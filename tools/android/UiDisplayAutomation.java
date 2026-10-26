import android.app.UiAutomation;
import android.graphics.Rect;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;
import org.json.JSONArray;
import org.json.JSONObject;

/** Shell-only UI acceptance helper, scoped to the driver's windows on an explicit display. */
public final class UiDisplayAutomation {
    public static void main(String[] args){
        Object wrapper=null;Class<?> type=null;int code=0;
        try{
            if(android.os.Process.myUid()!=2000||args.length<2)throw new IllegalArgumentException("Expected display and action");
            type=Class.forName("com.android.uiautomator.core.UiAutomationShellWrapper");
            java.lang.reflect.Constructor<?> constructor=type.getDeclaredConstructor();constructor.setAccessible(true);wrapper=constructor.newInstance();
            method(type,"connect").invoke(wrapper);
            UiAutomation automation=(UiAutomation)method(type,"getUiAutomation").invoke(wrapper);
            android.accessibilityservice.AccessibilityServiceInfo service=automation.getServiceInfo();
            service.flags|=android.accessibilityservice.AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS;
            automation.setServiceInfo(service);Thread.sleep(250);
            JSONArray nodes=new JSONArray();boolean[] acted={false};
            java.util.List<AccessibilityWindowInfo> windows=automation.getWindowsOnAllDisplays().get(Integer.parseInt(args[0]));
            if(windows!=null)for(AccessibilityWindowInfo window:windows){
                AccessibilityNodeInfo root=window.getRoot();
                if(root!=null&&"io.github.ms912x.desktop".contentEquals(root.getPackageName()))visit(root,args,nodes,acted);
            }
            System.out.println(new JSONObject().put("acted",acted[0]).put("nodes",nodes));
            if(!args[1].equals("dump")&&!acted[0])code=2;
        }catch(Throwable failure){failure.printStackTrace();code=1;}
        finally{if(wrapper!=null)try{method(type,"disconnect").invoke(wrapper);}catch(Exception ignored){ }}
        System.exit(code);
    }
    private static java.lang.reflect.Method method(Class<?> type,String name) throws Exception {
        java.lang.reflect.Method method=type.getDeclaredMethod(name);method.setAccessible(true);return method;
    }
    private static void visit(AccessibilityNodeInfo node,String[] args,JSONArray nodes,boolean[] acted) throws Exception {
        Rect bounds=new Rect();node.getBoundsInScreen(bounds);
        String text=node.isPassword()||"android.widget.EditText".contentEquals(node.getClassName())?"":String.valueOf(node.getText()==null?"":node.getText());
        String description=String.valueOf(node.getContentDescription()==null?"":node.getContentDescription());
        if(node.isVisibleToUser()){
            nodes.put(new JSONObject().put("text",text).put("description",description).put("class",node.getClassName())
                    .put("bounds",bounds.flattenToString()).put("enabled",node.isEnabled()).put("clickable",node.isClickable()));
            if(!acted[0]&&args[1].equals("click")&&args.length>2&&(text.equals(args[2])||description.equals(args[2])))
                acted[0]=node.performAction(AccessibilityNodeInfo.ACTION_CLICK);
            if(!acted[0]&&args[1].equals("scroll")&&node.isScrollable())acted[0]=node.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD);
        }
        for(int i=0;i<node.getChildCount();i++){AccessibilityNodeInfo child=node.getChild(i);if(child!=null)visit(child,args,nodes,acted);}
    }
}
