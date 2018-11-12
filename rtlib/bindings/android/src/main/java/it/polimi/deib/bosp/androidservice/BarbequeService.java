package it.polimi.deib.bosp.androidservice;

import android.app.Service;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.Messenger;
import android.os.RemoteException;
import android.support.annotation.Nullable;
import android.util.Log;

import bbque.rtlib.enumeration.RTLibExitCode;
import bbque.rtlib.exception.RTLibException;
import bbque.rtlib.exception.RTLibRegistrationException;
import bbque.rtlib.model.BbqueEXC;
import bbque.rtlib.model.RTLibServices;

/**
 * Created by mzanella on 24/04/18.
 */

public class BarbequeService extends Service {

    public static final String TAG = "BbqueService";

    //******* Available messages to the Service *******
    public static final int MSG_ISREGISTERED= 1;
    public static final int MSG_CREATE= 2;
    public static final int MSG_START= 3;
    public static final int MSG_WAIT_COMPLETION= 4;
    public static final int MSG_TERMINATE= 5;
    public static final int MSG_ENABLE= 6;
    public static final int MSG_DISABLE= 7;
    public static final int MSG_GET_CH_UID= 8;
    public static final int MSG_GET_UID= 9;
    public static final int MSG_SET_CPS= 10;
    public static final int MSG_SET_CTIME_US= 11;
    public static final int MSG_CYCLES= 12;
    public static final int MSG_DONE= 13;
    public static final int MSG_CURRENT_AWM= 14;
    //*************************************************

    private String name, recipe;

    public Intent intent = new Intent("it.polimi.dei.bosp.BBQUE_INTENT");

    private long creationTime = 0;

    public AndroidBbqueEXC mEXC;

    public RTLibServices rtlib;

    /** Within the onCreate we initialize the RTLib */
    @Override
    public void onCreate() {
        Log.d(TAG, "onCreated");
        creationTime = System.currentTimeMillis();
        super.onCreate();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "onStarted");
        return super.onStartCommand(intent, flags, startId);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.d(TAG, "onDestroyed");
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return mMessenger.getBinder();
    }

    /**
     * Follow the implementation of Activity2RTLib calls. This implementation
     * won't be overridden
     */

    protected void isRegistered(Messenger dest) {
        Log.d(TAG, "isRegistered?");
        boolean response = mEXC.isRegistered();
        Message msg = Message.obtain(null, MSG_ISREGISTERED,
                response ? 1 : 0,
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }
    /*
        private void create(Messenger dest, Object obj) {
            String messageString = obj.toString();
            String params[] = messageString.split("#");
            name = params[0];
            recipe = params[1];
            Log.d(TAG, "create, app: "+name+" with recipe "+recipe);
            int response = mEXC.create(name, recipe);
            Message msg = Message.obtain(null, MSG_CREATE,
                    response,
                    0);
            try {
                dest.send(msg);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }
    */
    private void start(Messenger dest) {
        Log.d(TAG, "start");
        RTLibExitCode response = null;
        try {
            mEXC.start();
            if(response == null){
                response = RTLibExitCode.RTLIB_OK;
            }
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_START,
                response.ordinal(),
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void waitCompletion(Messenger dest) {
        Log.d(TAG, "wait completion");
        RTLibExitCode response = null;
        try {
            mEXC.waitCompletion();
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_WAIT_COMPLETION,
                response.ordinal(),
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void terminate(Messenger dest) {
        Log.d(TAG, "terminate");
        RTLibExitCode response = null;
        try {
            mEXC.terminate();
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_TERMINATE,
                response.ordinal(),
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void enable(Messenger dest) {
        Log.d(TAG, "enable");
        RTLibExitCode response = RTLibExitCode.RTLIB_OK;
        try {
            mEXC.enable();
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_ENABLE,
                response.ordinal(),
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void disable(Messenger dest) {
        Log.d(TAG, "disable");
        RTLibExitCode response = RTLibExitCode.RTLIB_OK;
        try {
            mEXC.disable();
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_DISABLE,
                response.ordinal(),
                0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }
    /*
        protected void getChUid(Messenger dest) {
            Log.d(TAG, "getChUid");
            String response = mEXC.getChUid();
            Message msg = Message.obtain(null, MSG_GET_CH_UID,
                    response);
            try {
                dest.send(msg);
            } catch (RemoteException e) {
                e.printStackTrace();
            }
        }
    */
    protected void getUid(Messenger dest) {
        Log.d(TAG, "getUid");
        int response = mEXC.getUniqueID();
        Message msg = Message.obtain(null, MSG_GET_UID,
                response);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void setCPS(Messenger dest, float cps) {
        Log.d(TAG, "setCPS");
        RTLibExitCode response = RTLibExitCode.RTLIB_OK;
        try {
            mEXC.setCPS(cps);
        } catch (RTLibException e) {
            e.printStackTrace();
            response = e.getExitCode();
        }
        Message msg = Message.obtain(null, MSG_SET_CPS,
                response.ordinal(), 0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void getCTimeUS(Messenger dest, int us) {
        Log.d(TAG, "setCTimeUS");
        int response = mEXC.getCTimeUs();
        Message msg = Message.obtain(null, MSG_SET_CTIME_US,
                response, 0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void cycles(Messenger dest) {
        Log.d(TAG, "cycles");
        int response = mEXC.cycles();
        Message msg = Message.obtain(null, MSG_CYCLES,
                response, 0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void done(Messenger dest) {
        Log.d(TAG, "done");
        boolean response = mEXC.done();
        Message msg = Message.obtain(null, MSG_DONE,
                response);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }

    protected void currentAWM(Messenger dest) {
        Log.d(TAG, "currentAWM");
        int response = mEXC.currentAWM();
        Message msg = Message.obtain(null, MSG_CURRENT_AWM,
                response, 0);
        try {
            dest.send(msg);
        } catch (RemoteException e) {
            e.printStackTrace();
        }
    }


    /**
     * Instantiate the target - to be sent to clients - to communicate with
     * this instance of BbqueService.
     */
    final Messenger mMessenger = new Messenger(new BbqueMessageHandler());

    /**
     * Handler of incoming messages from clients.
     */

    protected class BbqueMessageHandler extends Handler {

        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case MSG_ISREGISTERED:
                    isRegistered(msg.replyTo);
                    break;
                case MSG_CREATE:
                    //create(msg.replyTo, msg.obj);
                    break;
                case MSG_START:
                    start(msg.replyTo);
                    break;
                case MSG_WAIT_COMPLETION:
                    waitCompletion(msg.replyTo);
                    break;
                case MSG_TERMINATE:
                    terminate(msg.replyTo);
                    break;
                case MSG_ENABLE:
                    enable(msg.replyTo);
                    break;
                case MSG_DISABLE:
                    disable(msg.replyTo);
                    break;
                case MSG_GET_CH_UID:
                    //getChUid(msg.replyTo);
                    break;
                case MSG_GET_UID:
                    getUid(msg.replyTo);
                    break;
                case MSG_SET_CPS:
                    setCPS(msg.replyTo, Float.valueOf(msg.obj.toString()));
                    break;
                case MSG_SET_CTIME_US:
                    getCTimeUS(msg.replyTo, msg.arg1);
                    break;
                case MSG_CYCLES:
                    cycles(msg.replyTo);
                    break;
                case MSG_DONE:
                    done(msg.replyTo);
                    break;
                case MSG_CURRENT_AWM:
                    currentAWM(msg.replyTo);
                    break;
                default:
                    super.handleMessage(msg);
            }
        }
    }

    protected class AndroidBbqueEXC extends BbqueEXC {

        public AndroidBbqueEXC(String s, String s1, RTLibServices rtLibServices) throws RTLibRegistrationException {
            super(s, s1, rtLibServices);
        }

        @Override
        protected void onSetup() throws RTLibException {
            Log.d(TAG,"onSetup called");
            intent.putExtra("BBQ_DEBUG", "onSetup called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
        }

        @Override
        protected void onConfigure(int i) throws RTLibException {
            Log.d(TAG,"onConfigure called");
            intent.putExtra("BBQ_DEBUG", "onConfigure called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
            int nr_cores = 0;
            //getAssignedResources(RTLibResourceType., nr_cores);
        }

        @Override
        protected void onSuspend() throws RTLibException {
            Log.d(TAG,"onSuspend called");
            intent.putExtra("BBQ_DEBUG", "onSuspend called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
        }

        @Override
        protected void onResume() throws RTLibException {
            Log.d(TAG,"onResume called");
            intent.putExtra("BBQ_DEBUG", "onResume called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
        }

        @Override
        protected void onRun() throws RTLibException {
            Log.d(TAG,"onRun called");
            intent.putExtra("BBQ_DEBUG", "onRun called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
        }

        @Override
        protected void onMonitor() throws RTLibException {
            Log.d(TAG,"onMonitor called");
            intent.putExtra("BBQ_DEBUG", "onMonitor called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
        }

        @Override
        protected void onRelease() throws RTLibException {
            Log.d(TAG,"onRelease called");
            intent.putExtra("BBQ_DEBUG", "onRelease called");
            intent.putExtra("INTENT_TIMESTAMP",
                    System.currentTimeMillis()-creationTime);
            intent.putExtra("APP_NAME", name);
            sendBroadcast(intent);
            terminate();
        }
    }

}

