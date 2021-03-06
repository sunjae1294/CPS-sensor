package com.example.cps_sunjae.sensorapp;

import android.Manifest;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.AsyncTask;
import android.os.Bundle;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.wearable.activity.WearableActivity;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import com.google.android.gms.tasks.Task;
import com.google.android.gms.wearable.Asset;
import com.google.android.gms.wearable.DataItem;
import com.google.android.gms.wearable.MessageClient;
import com.google.android.gms.wearable.MessageEvent;
import com.google.android.gms.wearable.PutDataMapRequest;
import com.google.android.gms.wearable.PutDataRequest;
import com.google.android.gms.wearable.Wearable;

import java.nio.ByteBuffer;
import java.util.ArrayList;


public class MainActivity extends WearableActivity implements SensorEventListener,
        MessageClient.OnMessageReceivedListener {

    private static final String TAG = "SensorAppW";
    private static final String START_SENSING_PATH = "/start-sensing";
    private static final String STOP_SENSING_PATH = "/stop-sensing";

    private static final String SENSOR_ACCEL = "sensor.accel";
    private static final String SENSOR_GYRO = "sensor.gyro";
    private static final String SENSOR_lACCEL = "sensor.laccel";
    private static final String SENSOR_GRAV = "sensor.grav";
    private static final String SENSOR_ROTVEC = "sensor.rotvec";
    private static final String SENSOR_ORIENT = "sensor.orient";
    private static final String RECORD = "record";

    private static final String recording = "Recording...";
    private static final String stop = "Stop";

    private static final float x_m = (float)1;
    private static final float x_c = (float)0;
    private static final float y_m = (float)1;
    private static final float y_c = (float)0;
    private static final float z_m = (float)1;
    private static final float z_c = (float)0;

    // Arrays to hold sensor data
    // Accelerometer
    private ArrayList<Long> accelT = new ArrayList<>();
    private ArrayList<Float> accelX = new ArrayList<>();
    private ArrayList<Float> accelY = new ArrayList<>();
    private ArrayList<Float> accelZ = new ArrayList<>();

    // Linear Acceleration
    private ArrayList<Long> lAccelT = new ArrayList<>();
    private ArrayList<Float> lAccelX = new ArrayList<>();
    private ArrayList<Float> lAccelY = new ArrayList<>();
    private ArrayList<Float> lAccelZ = new ArrayList<>();

    // Gyroscope
    private ArrayList<Long> gyroT = new ArrayList<>();
    private ArrayList<Float> gyroX = new ArrayList<>();
    private ArrayList<Float> gyroY = new ArrayList<>();
    private ArrayList<Float> gyroZ = new ArrayList<>();

    // Rotation Vector
    private ArrayList<Long> rotT = new ArrayList<>();
    private ArrayList<Float> rotX = new ArrayList<>();
    private ArrayList<Float> rotY = new ArrayList<>();
    private ArrayList<Float> rotZ = new ArrayList<>();
    private ArrayList<Float> rotW = new ArrayList<>();

    // Gravity
    private ArrayList<Long> gravT = new ArrayList<>();
    private ArrayList<Float>gravX = new ArrayList<>();
    private ArrayList<Float>gravY = new ArrayList<>();
    private ArrayList<Float>gravZ = new ArrayList<>();

    // Sensors
    private SensorManager mSensorManager;
    private Sensor mAccel;
    private Sensor mRot;

    // Audio Recorder
    private final int[] sampleRates = new int[] {48000, 44100, 22050, 11025, 8000, 16000};
    private final short[] audioFormats = new short[] {AudioFormat.ENCODING_PCM_16BIT}; //AudioFormat.ENCODING_PCM_8BIT
    private final short[] channelConfigs = new short[] {AudioFormat.CHANNEL_IN_MONO}; //AudioFormat.CHANNEL_IN_STEREO,
    private int mSampleRate;
    private short mAudioFormat, mChannelConfig;
    private AudioRecord mRecorder;
    private Thread mRecorderThread = null;
    private short recordTemp[] = new short[1024*1000];
    private int recordSize;
    private boolean isPlaying = false;

    // Sensor variables
    private boolean start = false;

    // TextView
    TextView currentLabel;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Log.d(TAG, "onCreate()");

        //Enables Always-on
        setAmbientEnabled();

        // Get an instance of the SensorManager
        mSensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);

        // Create the listeners for each sensor type
        mAccel = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        mRot = mSensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);

        // Set Labels
        currentLabel = findViewById(R.id.currentActivity);

        // Set Buttons
        findViewById(R.id.btn_start).setOnClickListener(
                new Button.OnClickListener() {
                    public void onClick(View v) {
                        startRecording();
                        startSensing();
                    }
                }
        );
        findViewById(R.id.btn_stop).setOnClickListener(
                new Button.OnClickListener() {
                    public void onClick(View v) {
                        stopRecording();
                        stopSensing();
                    }
                }
        );

        checkPermission();
    }

    public void checkPermission() {
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.RECORD_AUDIO}, 0);
        }
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.WAKE_LOCK) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.WAKE_LOCK}, 0);
        }
    }

    @Override
    public void onMessageReceived(MessageEvent messageEvent) {
        if (messageEvent.getPath().equals(START_SENSING_PATH)) {
            startRecording();
            startSensing();
        } else if (messageEvent.getPath().equals(STOP_SENSING_PATH)) {
            stopRecording();
            stopSensing();
        }
    }

    public void startRecording() {
        if(isPlaying) {
            Log.d(TAG, "Already recording!");
            return;
        }
        mRecorder = findRecorder();
        if (mRecorder == null) {
            Log.d(TAG, "null mRecorder");
            return;
        }
        mRecorder.startRecording();
        isPlaying = true;
        mRecorderThread = new Thread(new Runnable() {
            @Override
            public void run() {
                onRecording();
            }
        });
        mRecorderThread.start();
    }

    private void onRecording() {
        int i;
        for (i = 0 ; isPlaying ; i++) {
            if (i >= 1000) {
                this.runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        currentLabel.append("\nYou can't record over 20 seconds!");
                    }
                });
                break;
            }
            mRecorder.read(recordTemp, 1024*i, 1024);
        }
        recordSize = i;
    }
    public void stopRecording() {
        if (mRecorder != null) {
            isPlaying = false;
            mRecorder.stop();
            mRecorder.release();
            mRecorder = null;
            mRecorderThread = null;
        }
    }

    public void startSensing() {
        if (!start) {
            start = true;
            currentLabel.setText(recording);
            mSensorManager.registerListener(this, mAccel, SensorManager.SENSOR_DELAY_FASTEST);
            mSensorManager.registerListener(this, mRot, SensorManager.SENSOR_DELAY_FASTEST);
        }
    }

    public void stopSensing() {
        if (start) {
            start = false;
            mSensorManager.unregisterListener(this);
            currentLabel.setText("Sending...");
            new SendTask().execute();
        }
    }

    public void onSensorChanged(SensorEvent event) {
        if (start) {
            if (event.sensor.getType() == Sensor.TYPE_ACCELEROMETER) {
                getAcceleration(event);
            } else if (event.sensor.getType() == Sensor.TYPE_ROTATION_VECTOR) {
                getRotationVector(event);
            }
        }
    }

    private void getAcceleration(SensorEvent event) {
        float accX = (event.values[0] + x_c) * x_m;
        float accY = (event.values[1] + y_c) * y_m;
        float accZ = (event.values[2] + z_c) * z_m;

        if (start) {
            accelT.add(event.timestamp);
            accelX.add(accX);
            accelY.add(accY);
            accelZ.add(accZ);
        }
    }

    private void getLinearAcceleration(float[] rotVec, float[] accel) {
        float[] globalAcc = localToGlobal(rotVec, accel);

        globalAcc[2] -= (float)9.798;

        lAccelX.add(globalAcc[0]);
        lAccelY.add(globalAcc[1]);
        lAccelZ.add(globalAcc[2]);
    }

    private void getGyroscope(SensorEvent event) {
        float gyX = event.values[0];
        float gyY = event.values[1];
        float gyZ = event.values[2];

        // Record the values
        if (start) {
            gyroT.add(event.timestamp);
            gyroX.add(gyX);
            gyroY.add(gyY);
            gyroZ.add(gyZ);
        }
    }

    private void getRotationVector(SensorEvent event) {
        float rotVecX = event.values[0];
        float rotVecY = event.values[1];
        float rotVecZ = event.values[2];
        float rotVecW = event.values[3];

        // Record the values
        if (start) {
            rotT.add(event.timestamp);
            rotX.add(rotVecX);
            rotY.add(rotVecY);
            rotZ.add(rotVecZ);
            rotW.add(rotVecW);

            gravT.add(event.timestamp);
            calcGravity(event.values);

            if (accelT.size() > 0) {
                lAccelT.add(event.timestamp);
                getLinearAcceleration(event.values,
                        new float[]{accelX.get(accelT.size() - 1), accelY.get(accelT.size() - 1), accelZ.get(accelT.size() - 1), 0});
            }
        }
    }

    private void calcGravity(float[] rotVector) {
        float[] gravity = {0,0,(float)9.798, 0};
        float temp[] = new float[4];
        float result[] = new float[4];

        float[] rotVectorInverse = new float[4];
        float inverseDenom = (float)(Math.pow(rotVector[3],2) + Math.pow(rotVector[0], 2)
                + Math.pow(rotVector[1], 2) + Math.pow(rotVector[2], 2));
        rotVectorInverse[0] = (-1)*rotVector[0] / inverseDenom;
        rotVectorInverse[1] = (-1)*rotVector[1] / inverseDenom;
        rotVectorInverse[2] = (-1)*rotVector[2] / inverseDenom;
        rotVectorInverse[3] = rotVector[3] / inverseDenom;

        hamiltonProduct(rotVector, gravity, temp);
        hamiltonProduct(temp, rotVectorInverse, result);

        gravX.add(result[0]);
        gravY.add(result[1]);
        gravZ.add(result[2]);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Wearable.getMessageClient(this).addListener(this);
    }

    @Override
    protected void onPause() {
        //stopSensing();
        //Wearable.getMessageClient(this).removeListener(this);
        super.onPause();
    }

    @Override
    protected void onStop() {
        stopSensing();
        Wearable.getMessageClient(this).removeListener(this);
        Log.d(TAG, "onStop()");
        super.onStop();
    }

    private class SendTask extends AsyncTask<Void, Void, Void> {
        @Override
        protected Void doInBackground(Void... args) {
            sendSensorData();
            return null;
        }
    }

    public void sendSensorData(){
        String accel = "";
        String gyro = "";
        String lAccel = "";
        String rot = "";
        String grav = "";

        // Accelerometer
        for (int i = 0; i < accelT.size(); i++) {
            accel = accel.concat(Long.toString(accelT.get(i)) + "\t" +
                    Float.toString(accelX.get(i)) + "\t" +
                    Float.toString(accelY.get(i)) + "\t" +
                    Float.toString(accelZ.get(i)) + "\n");
        }
//
//        //GyroScope
//        for (int i = 0; i < gyroT.size(); i++) {
//            gyro = gyro.concat(Long.toString(gyroT.get(i)) + "\t" +
//                    Float.toString(gyroX.get(i)) + "\t" +
//                    Float.toString(gyroY.get(i)) + "\t" +
//                    Float.toString(gyroZ.get(i)) + "\n");
//        }

        // Linear Accelerometer
        for (int i = 0; i < lAccelT.size(); i++) {
            lAccel = lAccel.concat(Long.toString(lAccelT.get(i)) + "\t" +
                    Float.toString(lAccelX.get(i)) + "\t" +
                    Float.toString(lAccelY.get(i)) + "\t" +
                    Float.toString(lAccelZ.get(i)) + "\n");
        }

        // Rotation Vector
        for (int i = 0; i < rotT.size(); i++) {
            rot = rot.concat(rotT.get(i) + "\t" +
                            rotX.get(i) + "\t" +
                            rotY.get(i) + "\t" +
                            rotZ.get(i) + "\t" +
                            rotW.get(i) + "\n");
        }

        // Gravity
        for (int i = 0; i < gravT.size(); i++) {
            grav = grav.concat(Long.toString(gravT.get(i)) + "\t" +
                    Float.toString(gravX.get(i)) + "\t" +
                    Float.toString(gravY.get(i)) + "\t" +
                    Float.toString(gravZ.get(i)) + "\n");
        }

        short[] recordData = new short[1024*recordSize];
        for(int i = 0; i < recordSize; i++) {
            for(int j = 0; j < 1024; j++) {
                recordData[i*1024 + j] = recordTemp[i*1024 + j];
            }
        }

        Log.d("testdrive", "sending data");

        PutDataMapRequest putDataMapReq = PutDataMapRequest.create("/sensor");
        Asset accelAsset = Asset.createFromBytes(accel.getBytes());
//        Asset gyroAsset = Asset.createFromBytes(gyro.getBytes());
        Asset gravAsset = Asset.createFromBytes(grav.getBytes());
        Asset lAccelAsset = Asset.createFromBytes(lAccel.getBytes());
        Asset rotVecAsset = Asset.createFromBytes(rot.getBytes());
        Asset recordAsset = Asset.createFromBytes(short2byte(recordData));

        putDataMapReq.getDataMap().putAsset(SENSOR_ACCEL, accelAsset);
//        putDataMapReq.getDataMap().putAsset(SENSOR_GYRO, gyroAsset);
        putDataMapReq.getDataMap().putAsset(SENSOR_GRAV,gravAsset);
        putDataMapReq.getDataMap().putAsset(SENSOR_lACCEL,lAccelAsset);
        putDataMapReq.getDataMap().putAsset(SENSOR_ROTVEC,rotVecAsset);
        putDataMapReq.getDataMap().putAsset(RECORD, recordAsset);

        PutDataRequest putDataReq = putDataMapReq.asPutDataRequest().setUrgent();

        Task<DataItem> putDataTask = Wearable.getDataClient(this).putDataItem(putDataReq);

        accelT.clear();
        accelX.clear();
        accelY.clear();
        accelZ.clear();
        lAccelT.clear();
        lAccelX.clear();
        lAccelY.clear();
        lAccelZ.clear();
        gyroT.clear();
        gyroX.clear();
        gyroY.clear();
        gyroZ.clear();
        rotT.clear();
        rotX.clear();
        rotY.clear();
        rotZ.clear();
        rotW.clear();
        gravT.clear();
        gravX.clear();
        gravY.clear();
        gravZ.clear();

        recordTemp = new short[1024*1000];

        Log.d("testdrive", "sent Data");

        this.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                currentLabel.setText(stop);
            }
        });
    }

    private AudioRecord findRecorder() {
        for (int rate : sampleRates) {
            for (short format : audioFormats) {
                for (short channel : channelConfigs) {
                    try {
                        int bufferSize = AudioRecord.getMinBufferSize(rate, channel, format);
                        if (bufferSize != AudioRecord.ERROR_BAD_VALUE) {
                            mSampleRate = rate;
                            mAudioFormat = format;
                            mChannelConfig = channel;
                            AudioRecord recorder = new AudioRecord(MediaRecorder.AudioSource.MIC, mSampleRate, mChannelConfig, mAudioFormat, bufferSize);
                            if (recorder.getState() == AudioRecord.STATE_INITIALIZED) {
                                Log.d(TAG, "SampleRate : " + mSampleRate + ", AudioFormat : " + mAudioFormat + ", Channel : " + mChannelConfig);
                                return recorder;
                            }
                        }
                    } catch (Exception e) {
                        e.printStackTrace();
                    }
                }
            }
        }
        return null;
    }

    private byte[] short2byte(short[] sData) {
        int shortSize = sData.length;
        byte[] bytes = new byte[shortSize * 2];
        for (int i = 0; i < shortSize; i++) {
            bytes[i * 2] = (byte) (sData[i] & 0x00FF);
            bytes[(i * 2) + 1] = (byte) (sData[i] >> 8);
            sData[i] = 0;
        }
        return bytes;
    }

    private float[] localToGlobal(float[] rotVector, float[] accel) {
        float temp[] = new float[4];
        float result[] = new float[4];

        float[] rotVectorInverse = new float[4];
        float inverseDenom = (float)(Math.pow(rotVector[3],2) + Math.pow(rotVector[0], 2)
                + Math.pow(rotVector[1], 2) + Math.pow(rotVector[2], 2));
        rotVectorInverse[0] = (-1)*rotVector[0] / inverseDenom;
        rotVectorInverse[1] = (-1)*rotVector[1] / inverseDenom;
        rotVectorInverse[2] = (-1)*rotVector[2] / inverseDenom;
        rotVectorInverse[3] = rotVector[3] / inverseDenom;

        hamiltonProduct(rotVectorInverse, accel, temp);
        hamiltonProduct(temp, rotVector, result);

        return new float[]{result[0], result[1], result[2]};
    }

    private void hamiltonProduct(float[] x, float[] y, float[] result) {
        result[0] = (x[3]*y[0]) + (x[0]*y[3]) - (x[1]*y[2]) + (x[2]*y[1]);
        result[1] = (x[3]*y[1]) + (x[0]*y[2]) + (x[1]*y[3]) - (x[2]*y[0]);
        result[2] = (x[3]*y[2]) - (x[0]*y[1]) + (x[1]*y[0]) + (x[2]*y[3]);
        result[3] = (x[3]*y[3]) - (x[0]*y[0]) - (x[1]*y[1]) - (x[2]*y[2]);
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int i){
    }
}

