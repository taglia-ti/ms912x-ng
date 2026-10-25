package io.github.ms912x.helper;

import android.app.ActivityOptions;
import android.content.ComponentName;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.AttributionSource;
import android.content.Intent;
import android.graphics.Rect;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.hardware.display.VirtualDisplayConfig;
import android.os.Build;
import android.os.Handler;
import android.view.Surface;
import java.lang.reflect.Method;

/** Framework context and screen ownership stay in the real shell-UID process. */
final class ShellDesktop implements AutoCloseable {
    private static Context context;
    private VirtualDisplay display;
    private final int width,height;

    private static Context context() throws Exception {
        if(context==null){
            Class<?> thread=Class.forName("android.app.ActivityThread");
            Object activityThread=thread.getMethod("currentActivityThread").invoke(null);
            if(activityThread==null){
                java.lang.reflect.Constructor<?> constructor=thread.getDeclaredConstructor();constructor.setAccessible(true);
                activityThread=constructor.newInstance();
                java.lang.reflect.Field current=thread.getDeclaredField("sCurrentActivityThread");current.setAccessible(true);current.set(null,activityThread);
                java.lang.reflect.Field systemThread=thread.getDeclaredField("mSystemThread");systemThread.setAccessible(true);systemThread.setBoolean(activityThread,true);
                if(Build.VERSION.SDK_INT>=31){
                    Class<?> controller=Class.forName("android.app.ConfigurationController");
                    java.lang.reflect.Constructor<?> configuration=controller.getDeclaredConstructor(Class.forName("android.app.ActivityThreadInternal"));configuration.setAccessible(true);
                    java.lang.reflect.Field field=thread.getDeclaredField("mConfigurationController");field.setAccessible(true);field.set(activityThread,configuration.newInstance(activityThread));
                }
            }
            Class<?> bindType=Class.forName("android.app.ActivityThread$AppBindData");
            java.lang.reflect.Constructor<?> bindConstructor=bindType.getDeclaredConstructor();bindConstructor.setAccessible(true);
            Object bind=bindConstructor.newInstance();
            android.content.pm.ApplicationInfo info=new android.content.pm.ApplicationInfo();info.packageName="com.android.shell";info.uid=android.os.Process.myUid();
            java.lang.reflect.Field appInfo=bindType.getDeclaredField("appInfo");appInfo.setAccessible(true);appInfo.set(bind,info);
            java.lang.reflect.Field bound=thread.getDeclaredField("mBoundApplication");bound.setAccessible(true);bound.set(activityThread,bind);
            Context system=(Context)thread.getMethod("getSystemContext").invoke(activityThread);
            context=Build.VERSION.SDK_INT>=31?new AttributedContext(system):new PackageContext(system);
            java.lang.reflect.Field initial=thread.getDeclaredField("mInitialApplication");initial.setAccessible(true);
            initial.set(activityThread,android.app.Instrumentation.newApplication(android.app.Application.class,context));
            if(!"com.android.shell".equals(context.getPackageName()))throw new SecurityException("Shell attribution unavailable");
        }
        return context;
    }
    private static class PackageContext extends ContextWrapper {
        PackageContext(Context base){super(base);}
        @Override public String getPackageName(){return "com.android.shell";}
        @Override public String getOpPackageName(){return "com.android.shell";}
        @Override public Context getApplicationContext(){return this;}
    }
    @android.annotation.TargetApi(31)
    private static final class AttributedContext extends PackageContext {
        AttributedContext(Context base){super(base);}
        @Override public AttributionSource getAttributionSource(){return new AttributionSource.Builder(android.os.Process.myUid()).setPackageName("com.android.shell").build();}
    }

    ShellDesktop(String name,int width,int height,int density,int refresh,Surface surface) throws Exception {
        this.width=width;this.height=height;
        Context shell=context();
        java.lang.reflect.Constructor<DisplayManager> constructor=DisplayManager.class.getDeclaredConstructor(Context.class);
        constructor.setAccessible(true);
        DisplayManager manager=constructor.newInstance(shell);
        int flags=DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC|DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION
                |DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY|(1<<6)|(1<<8)|(1<<9);
        if(Build.VERSION.SDK_INT>=33)flags|=(1<<10)|(1<<11)|(1<<12)|(1<<13);
        if(Build.VERSION.SDK_INT>=34)flags|=(1<<14)|(1<<15);
        if(Build.VERSION.SDK_INT>=34){
            display=manager.createVirtualDisplay(new VirtualDisplayConfig.Builder(name,width,height,density)
                    .setSurface(surface).setFlags(flags).setRequestedRefreshRate(refresh).build());
        }else display=manager.createVirtualDisplay(name,width,height,density,surface,flags);
        if(display==null)throw new IllegalStateException("Independent screen creation returned null");
    }
    int id(){return display==null?-1:display.getDisplay().getDisplayId();}
    void home(String appPackage) throws Exception {
        try{start(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_SECONDARY_HOME));return;}
        catch(IllegalStateException unsupportedSecondaryHome){ /* Some API 30 launchers reject secondary homes. */ }
        try{start(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME));return;}
        catch(IllegalStateException unsupportedHome){ /* Keep creation useful without a compatible system launcher. */ }
        start(new Intent(Intent.ACTION_MAIN).setComponent(new ComponentName(appPackage,"io.github.ms912x.desktop.DesktopStartActivity")));
    }
    void launch(String component) throws Exception {
        ComponentName target=ComponentName.unflattenFromString(component);
        if(target==null||component.length()>512)throw new IllegalArgumentException("Invalid app component");
        start(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setComponent(target),true);
    }
    DisplayInputController inputController(Handler handler,String appPackage,DisplayInputController.Error error) throws Exception {
        PointerOverlay pointer=null;
        try{pointer=new PointerOverlay(handler,context(),id(),width,height);}
        catch(RuntimeException unavailable){android.util.Log.e("MS912xHelper","Cursor overlay unavailable",unavailable);}
        return new DisplayInputController(handler,new FrameworkInputCompat(context()),
                pointer,id(),width,height,
                ()->home(appPackage),error);
    }
    private void start(Intent intent) throws Exception {
        start(intent,false);
    }
    private void start(Intent intent,boolean preferFreeform) throws Exception {
        if(display==null)throw new IllegalStateException("Independent screen is closed");
        ActivityOptions options=ActivityOptions.makeBasic();options.setLaunchDisplayId(id());
        if(preferFreeform){
            // Samsung DeX and Android desktop shells use Android's freeform
            // window mode. This is a best-effort request; Android may still
            // choose fullscreen when the display or app does not allow it.
            try{
                ActivityOptions.class.getMethod("setLaunchWindowingMode",int.class).invoke(options,5);
                int marginX=Math.max(0,width/8),marginY=Math.max(0,height/8);
                options.setLaunchBounds(new Rect(marginX,marginY,width-marginX,height-marginY));
            }catch(ReflectiveOperationException unavailable){ /* Older Android uses fullscreen. */ }
        }
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_MULTIPLE_TASK);
        Object activity=Class.forName("android.app.ActivityTaskManager").getMethod("getService").invoke(null);
        Method start=null;
        for(Method method:Class.forName("android.app.IActivityTaskManager").getMethods())
            if(method.getName().equals("startActivityAsUser")&&method.getParameterCount()==12){start=method;break;}
        if(start==null)throw new NoSuchMethodException("Activity start for shell is unavailable");
        int result=(Integer)start.invoke(activity,null,"com.android.shell",null,intent,null,null,null,0,0,null,options.toBundle(),0);
        if(result<0&&preferFreeform){start(intent,false);return;}
        if(result<0)throw new IllegalStateException("Android rejected app launch: "+result);
    }
    @Override public void close(){if(display!=null){VirtualDisplay old=display;display=null;old.release();}}
}
