package io.github.taglia_ti.ms912x_ng.desktop;

import android.content.Intent;
import android.hardware.display.VirtualDisplay;

/** Producer ownership is separate from reader ownership so USB can drain safely. */
interface CaptureSource {
    final class Request {
        final long generation;
        final int sourceId, rotation, densityDpi;
        final String identity;
        final DisplayModeOption mode;
        final boolean output, helper;
        Request(long generation,int sourceId,String identity,DisplayModeOption mode,int rotation,int densityDpi,boolean output) {
            this(generation,sourceId,identity,mode,rotation,densityDpi,output,false);
        }
        Request(long generation,int sourceId,String identity,DisplayModeOption mode,int rotation,int densityDpi,boolean output,boolean helper) {
            this.generation=generation;this.sourceId=sourceId;this.identity=identity;this.mode=mode;
            this.rotation=rotation;this.densityDpi=densityDpi;this.output=output;this.helper=helper;
        }
    }
    interface Listener {
        void onReady(CaptureController capture);
        void onChanged();
        void onEnded(String reason);
        default void onConsentRequired() { }
    }
    void start(Request request,Listener listener);
    void stopProducer(Runnable stopped);
    void releaseReader(Runnable released);
    CaptureController capture();
    String status();
    default String launchStatus(){return "App launch probe not run.";}
    default VirtualDisplay ownedDisplay(){return null;}
    default TestScreenPresentation presentation(){return null;}
    default int createdDisplayId(){return -1;}
    default boolean inputSupported(){return false;}
    default String inputStatus(){return "Touchpad input is available for an ADB-created screen.";}
    default void sendInput(int type,float x,float y,int button){}
    default void launchApp(String component){}
    default void toggleScene(){ }
    default void consent(int resultCode,Intent data){ }
}
