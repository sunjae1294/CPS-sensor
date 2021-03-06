/* *******************************************************************************
 *	                              kinectTracker3
 * 
 * This program records users body joint and color marker's position in 3D space
 * and then writes it into a file named 
 *
 * Usage:
 * 1. Run program, check the thresholded image (black-white) captures the marker
 *		if not, manually adjust HSV color range in function loadHSVRange()
 * 2. Click 'click to record' button
 *		The camera image will freeze. this is OK and it is for optimization.
 *		(it tracks objects in real time and display is a huge overhead)
 *		UI doesn't have any effect. See console for feedback
 * 3. Click button again to stop recording and wait a little bit
 *		Or it stops automatically when it reaches max_frames
 * 4. Now the file is saved in kindata.txt.
 * 5. Parse kindata.txt with MATLAB and save the result with some name.
 * 6. You can now record again without restarting the program
 *
 * - mostly written by Eunhyouk Shin
 * - object tracking is based on this tutorial:
	https://www.youtube.com/watch?v=bSeFrPrqZ2A
 *********************************************************************************/

#include "stdafx.h"

#include <stdio.h>

#include <sstream>
#include <string>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2\highgui\highgui.hpp>

#include <Windows.h>
#include <Ole2.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <gl/glut.h>

#include <Kinect.h>

#define LINE_BUF_SIZE 128

using namespace std;
using namespace cv;

// window names
const string checkWindowName = "Check Window";
const string thresWindowName = "Threshold Window";
const string controlWindowName = "Control Window";

// Tracking variables
int H_MIN, H_MAX, S_MIN, S_MAX, V_MIN, V_MAX;
bool recording;
bool stopped;

// body tracking
BOOLEAN tracked;
Joint joints[JointType_Count];

// Kinect constants
const int d_width = 512;
const int d_height = 424;
const int c_width = 1920;
const int c_height = 1080;

// Kinect buffers
unsigned char rgbimage[c_width*c_height * 4];
CameraSpacePoint rgb2xyz[c_width*c_height];

// Kinect variables
IKinectSensor* sensor;
IMultiSourceFrameReader* reader;
ICoordinateMapper* mapper;

// Control panel
Mat3b ctrl_canvas;
Rect record_button;

// Marker tracking constants
const int MAX_NUM_OBJECTS = 50;
const int MIN_OBJECT_AREA = 20 * 20;

const float small_ratio = 0.5;
const int s_width = int(c_width * small_ratio);
const int s_height = int(c_height * small_ratio);
const float local_ratio = 0.2; // < 0.5
const int local_size = int(s_height * local_ratio);

// Recording variables
struct point_data {
	float X;
	float Y;
	float Z;
};

// Recorded data for one time frame
struct record_frame {
	SYSTEMTIME st; // system time (time stamp)
	bool haveMarker;
	struct point_data mp; // marker position
	bool haveBody;
	struct point_data ls; // left shoulder
	struct point_data le; // left elbow
	struct point_data lw; // left wrist
	struct point_data ss; // soulder spine
};

struct record_frame * record_buffer;
const int max_frames = 10000; // maximum number of frames to record at once
int next_rec;
ofstream stream;
char linebuf[LINE_BUF_SIZE];

/* Load the marker object's color range in HSV coordinates which are experimentally determined.
 currently just hardcoded inside this function. */
void loadHSVRange(int * hm, int * hM, int * sm, int * sM, int * vm, int * vM) {
	// currently hard-coded
	
	/*
	// blue sponge at desk
	*hm = 83;
	*hM = 100;
	*sm = 102;
	*sM = 202;
	*vm = 16;
	*vM = 192;
	*/

	// blue sponge at door side, full lighting (the setting we used for recording so far)
	*hm = 76;
	*hM = 102;
	*sm = 112;
	*sM = 256;
	*vm = 171;
	*vM = 256;

}


/* --------------------------------------------------
	Kinect functions
-------------------------------------------------- */

/* Initialize Kinect for acuqiring depth, color, bodytracking data */
bool initKinect() {
	if (FAILED(GetDefaultKinectSensor(&sensor))) {
		return false;
	}
	if (sensor) {
		sensor->get_CoordinateMapper(&mapper);
		sensor->Open();
		sensor->OpenMultiSourceFrameReader(
			FrameSourceTypes::FrameSourceTypes_Depth 
			| FrameSourceTypes::FrameSourceTypes_Color
			| FrameSourceTypes::FrameSourceTypes_Body,
			&reader);
		return reader;
	}
	else {
		return false;
	}
}

/* Get depth information from Kinect and sets ColorSpace->CameraSpace mapping (rgb2xyz)*/
void getDepthData(IMultiSourceFrame* frame) {
	IDepthFrame* depthframe;
	IDepthFrameReference* frameref = NULL;
	frame->get_DepthFrameReference(&frameref);
	frameref->AcquireFrame(&depthframe);
	if (frameref) frameref->Release();
	if (!depthframe) return;
	// Get data from frame
	unsigned int sz;
	unsigned short* buf;
	depthframe->AccessUnderlyingBuffer(&sz, &buf);
	mapper->MapColorFrameToCameraSpace(d_width*d_height, buf, c_width*c_height, rgb2xyz);
	if (depthframe) depthframe->Release();
}

/* Get color information from Kinect. Basically just camera video. (rgbimage) */
void getRgbData(IMultiSourceFrame* frame) {
	IColorFrame* colorframe;
	IColorFrameReference* frameref = NULL;
	frame->get_ColorFrameReference(&frameref);
	frameref->AcquireFrame(&colorframe);
	if (frameref) frameref->Release();
	if (!colorframe) return;
	// Get data from frame
	colorframe->CopyConvertedFrameDataToArray(c_width*c_height * 4, rgbimage, ColorImageFormat_Bgra);
	if (colorframe) colorframe->Release();
}

/* Get bodytracking information of all joints for one person. (joints) */
void getBodyData(IMultiSourceFrame* frame) {
	IBodyFrame* bodyframe;
	IBodyFrameReference* frameref = NULL;
	DetectionResult dr;
	frame->get_BodyFrameReference(&frameref);
	frameref->AcquireFrame(&bodyframe);
	if (frameref) frameref->Release();

	if (!bodyframe) return;

	IBody* body[6] = { 0 };
	bodyframe->GetAndRefreshBodyData(6, body);
	for (int i = 0; i < 6; i++) {
		body[i]->get_IsTracked(&tracked);
		if (tracked) {
			body[i]->GetJoints(JointType_Count, joints);
			break;
		}
	}

	if (bodyframe) bodyframe->Release();
}

/* Single wrapper for all Kinect get functions */
void getKinectData() {
	IMultiSourceFrame* frame = NULL;
	if (SUCCEEDED(reader->AcquireLatestFrame(&frame))) {
		getDepthData(frame);
		getRgbData(frame);
		getBodyData(frame);
	}
	if (frame) frame->Release();
}


/* --------------------------------------------------
	UI functions
-------------------------------------------------- */

/* Record button callback */
void controlClickCallback(int event, int x, int y, int flags, void* userdata) {
	if (event == EVENT_LBUTTONDOWN) {
		if (record_button.contains(Point(x, y))) {
			if (!recording) {
				next_rec = 0;
				cout << "Start recording..." << endl;
				stream.open("kindata.txt");
				recording = true;
			}
			else {
				cout << "Stopped!" << endl;
				stopped = true;
			}
		}
	}
}

/* Record button display */
void makeControlPanel() {
	string recButtonText("Click to record");
	Mat3b img(300, 300, Vec3b(0, 255, 0));
	record_button = Rect(0, 0, img.cols, 50);
	ctrl_canvas = Mat3b(img.rows + record_button.height, img.cols, Vec3b(0, 0, 0));
	ctrl_canvas(record_button) = Vec3b(200, 200, 200);
	putText(ctrl_canvas(record_button), recButtonText, Point(record_button.width*0.25,
		record_button.height*0.7), FONT_HERSHEY_PLAIN, 1, Scalar(0, 0, 0));
	img.copyTo(ctrl_canvas(Rect(0, record_button.height, img.cols, img.rows)));
	namedWindow(controlWindowName);
	setMouseCallback(controlWindowName, controlClickCallback);
	imshow(controlWindowName, ctrl_canvas);
}

/* Display arm to check before recording */
void drawArm(Mat& smallRGBimg) {
	const int jtCnt = 4;
	CameraSpacePoint cameraPoints[jtCnt];
	ColorSpacePoint colorPoints[jtCnt];
	cameraPoints[0] = joints[JointType_ShoulderLeft].Position;
	cameraPoints[1] = joints[JointType_ElbowLeft].Position;
	cameraPoints[2] = joints[JointType_WristLeft].Position;
	cameraPoints[3] = joints[JointType_SpineShoulder].Position;
	mapper->MapCameraPointsToColorSpace(jtCnt, cameraPoints, jtCnt, colorPoints);
	Point ls(int(colorPoints[0].X*small_ratio), int(colorPoints[0].Y*small_ratio));
	Point le(int(colorPoints[1].X*small_ratio), int(colorPoints[1].Y*small_ratio));
	Point lw(int(colorPoints[2].X*small_ratio), int(colorPoints[2].Y*small_ratio));
	Point ss(int(colorPoints[3].X*small_ratio), int(colorPoints[3].Y*small_ratio));
	circle(smallRGBimg, ls, 10, Vec3b(0, 0, 255), 5);
	circle(smallRGBimg, le, 10, Vec3b(255, 0, 255), 5);
	circle(smallRGBimg, lw, 10, Vec3b(255, 255, 0), 5);
	circle(smallRGBimg, ss, 10, Vec3b(0, 255, 0), 5);
	line(smallRGBimg, ss, ls, Vec3b(255, 255, 255), 5);
	line(smallRGBimg, ls, le, Vec3b(255, 255, 255), 5);
	line(smallRGBimg, le, lw, Vec3b(255, 255, 255), 5);
}


/* --------------------------------------------------
	 Marker tracking functions
   -------------------------------------------------- */

/* Make the thresholded image less noisy */
void morphOps(Mat &thresh) {
	//create structuring element that will be used to "dilate" and "erode" image.
	//the element chosen here is a 3px by 3px rectangle

	Mat erodeElement = getStructuringElement(MORPH_RECT, Size(3, 3));
	//dilate with larger element so make sure object is nicely visible
	Mat dilateElement = getStructuringElement(MORPH_RECT, Size(8, 8));

	erode(thresh, thresh, erodeElement);
	erode(thresh, thresh, erodeElement);

	dilate(thresh, thresh, dilateElement);
	dilate(thresh, thresh, dilateElement);
}

/* Set x, y to center of the object in the filtered image */
bool trackFilteredObject(int &x, int &y, Mat threshold) {

	Mat temp;
	threshold.copyTo(temp);
	//these two vectors needed for output of findContours
	vector< vector<Point> > contours;
	vector<Vec4i> hierarchy;
	//find contours of filtered image using openCV findContours function
	findContours(temp, contours, hierarchy, CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE);
	//use moments method to find our filtered object
	double refArea = 0;
	bool objectFound = false;
	if (hierarchy.size() > 0) {
		int numObjects = hierarchy.size();
		//if number of objects greater than MAX_NUM_OBJECTS we have a noisy filter
		if (numObjects<MAX_NUM_OBJECTS) {
			for (int index = 0; index >= 0; index = hierarchy[index][0]) {

				Moments moment = moments((cv::Mat)contours[index]);
				double area = moment.m00;

				//if the area is less than 20 px by 20px then it is probably just noise
				//if the area is the same as the 3/2 of the image size, probably just a bad filter
				//we only want the object with the largest area so we safe a reference area each
				//iteration and compare it to the area in the next iteration.
				if (area>MIN_OBJECT_AREA && area>refArea) {
					x = moment.m10 / area;
					y = moment.m01 / area;
					objectFound = true;
					refArea = area;
				}
				//else objectFound = false;
			}
		}
	}
	return objectFound;
}


/* --------------------------------------------------
	Recording functions
-------------------------------------------------- */

/* Write marker tracking information */
void writeMarker(int x, int y) {
	CameraSpacePoint& mp = rgb2xyz[c_width*int(y / small_ratio) + int(x / small_ratio)];
	record_buffer[next_rec].mp.X = mp.X;
	record_buffer[next_rec].mp.Y = mp.Y;
	record_buffer[next_rec].mp.Z = mp.Z;
	record_buffer[next_rec].haveMarker = true;
}

/* Write body tracking information */
void writeBody() {
	CameraSpacePoint& ls = joints[JointType_ShoulderLeft].Position;
	CameraSpacePoint& le = joints[JointType_ElbowLeft].Position;
	CameraSpacePoint& lw = joints[JointType_WristLeft].Position;
	CameraSpacePoint& ss = joints[JointType_SpineShoulder].Position;

	record_buffer[next_rec].ls.X = ls.X;
	record_buffer[next_rec].ls.Y = ls.Y;
	record_buffer[next_rec].ls.Z = ls.Z;

	record_buffer[next_rec].le.X = le.X;
	record_buffer[next_rec].le.Y = le.Y;
	record_buffer[next_rec].le.Z = le.Z;

	record_buffer[next_rec].lw.X = lw.X;
	record_buffer[next_rec].lw.Y = lw.Y;
	record_buffer[next_rec].lw.Z = lw.Z;

	record_buffer[next_rec].ss.X = ss.X;
	record_buffer[next_rec].ss.Y = ss.Y;
	record_buffer[next_rec].ss.Z = ss.Z;

	record_buffer[next_rec].haveBody = true;
}

/* Write recorded data into file stream */
void saveRecordedData() {
	int n_frames = next_rec;
	struct record_frame * rf;
	for (int i = 0; i < n_frames; i++) {
		rf = &(record_buffer[i]);

		// time stamp
		sprintf_s(linebuf, LINE_BUF_SIZE, "%d.%03d\t", rf->st.wMinute*60 + rf->st.wSecond, rf->st.wMilliseconds);
		stream << linebuf;

		// marker position
		if (rf->haveMarker) {
			sprintf_s(linebuf, LINE_BUF_SIZE, "1\t%f\t%f\t%f\t", rf->mp.X, rf->mp.Y, rf->mp.Z);
			stream << linebuf;
		}
		else {
			sprintf_s(linebuf, LINE_BUF_SIZE, "-1\t%f\t%f\t%f\t", 0, 0, 0);
			stream << linebuf;
		}
		
		// arm position
		if (rf->haveBody) {
			sprintf_s(linebuf, LINE_BUF_SIZE, "1\t%f\t%f\t%f\t", rf->ls.X, rf->ls.Y, rf->ls.Z);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", rf->le.X, rf->le.Y, rf->le.Z);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", rf->lw.X, rf->lw.Y, rf->lw.Z);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", rf->ss.X, rf->ss.Y, rf->ss.Z);
			stream << linebuf;
		}
		else {
			sprintf_s(linebuf, LINE_BUF_SIZE, "-1\t%f\t%f\t%f\t",0, 0, 0);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", 0, 0, 0);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", 0, 0, 0);
			stream << linebuf;
			sprintf_s(linebuf, LINE_BUF_SIZE, "%f\t%f\t%f\t", 0, 0, 0);
			stream << linebuf;
		}

		stream << endl;
	}
}

/* main routine */
int main()
{
	loadHSVRange(&H_MIN, &H_MAX, &S_MIN, &S_MAX, &V_MIN, &V_MAX);
	makeControlPanel();

	Mat fullRGBimg;
	Mat HSVimg;
	Mat thresImg;
	Mat smallRGBimg;
	Mat localRGBimg;

	record_buffer = (struct record_frame *)malloc(max_frames * sizeof(struct record_frame));

	int x, y, prev_x, prev_y;

	Rect interior(local_size, local_size, s_width - 2 * local_size, s_height - 2 * local_size);

	if (!initKinect()) return 1;
	recording = false;
	bool marker = true;
	bool markerFound = false;
	try {
		while (1) {
			getKinectData();
			GetSystemTime(&(record_buffer[next_rec].st));
			fullRGBimg = Mat(c_height, c_width, CV_8UC4, (void *)rgbimage);
			resize(fullRGBimg, smallRGBimg, cv::Size(s_width, s_height), 0, 0, INTER_NEAREST);
			if (marker) {
				if (markerFound) {
					// local search
					Rect localRect(prev_x - local_size, prev_y - local_size, 2 * local_size, 2 * local_size);
					localRGBimg = smallRGBimg(localRect);
					cvtColor(localRGBimg, HSVimg, COLOR_BGR2HSV);
					inRange(HSVimg, Scalar(H_MIN, S_MIN, V_MIN), Scalar(H_MAX, S_MAX, V_MAX), thresImg);
					morphOps(thresImg);
					markerFound = trackFilteredObject(x, y, thresImg);
					if (markerFound) {
						x = x + prev_x - local_size - 1;
						y = y + prev_y - local_size - 1;
					}
				}
				else {
					// full search
					cvtColor(smallRGBimg, HSVimg, COLOR_BGR2HSV);
					inRange(HSVimg, Scalar(H_MIN, S_MIN, V_MIN), Scalar(H_MAX, S_MAX, V_MAX), thresImg);
					morphOps(thresImg);
					markerFound = trackFilteredObject(x, y, thresImg);
				}
				markerFound = markerFound && interior.contains(Point(x, y));
				if (markerFound) {
					prev_x = x;
					prev_y = y;
				}
			}
			if (recording) {
				if (marker) {
					if (markerFound) {
						writeMarker(x, y);
					}
					else {
						record_buffer[next_rec].haveMarker = false;
					}
				}
				else {
					record_buffer[next_rec].haveMarker = false;
				}
				if (tracked) {
					writeBody();
				}
				else {
					record_buffer[next_rec].haveBody = false;
				}
				next_rec++;
				if (next_rec >= max_frames) {
					stopped = true;
				}
			}
			else {
				// preview phase
				if (tracked) {
					drawArm(smallRGBimg);
				}
				if (marker && markerFound) {
					circle(smallRGBimg, Point(x, y), 20, Scalar(0, 255, 0), 2);
				}
				// display
				imshow(checkWindowName, smallRGBimg);
				imshow(thresWindowName, thresImg);
			}
			waitKey(5);
			if (stopped) {
				saveRecordedData();
				cout << "saved recorded data" << endl;
				stream.close();
				stopped = false;
				recording = false;
				next_rec = 0;
			}
		}
	}
	catch (cv::Exception & e) {
		cerr << e.msg << endl; // output exception message
		waitKey(0);
	}
	// save recorded data to .txt file
    return 0;
}

