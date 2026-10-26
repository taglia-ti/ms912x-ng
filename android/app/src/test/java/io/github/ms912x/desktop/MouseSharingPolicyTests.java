package io.github.ms912x.desktop;

public final class MouseSharingPolicyTests {
    private static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
    private static MouseSharingInfo ready(){return new MouseSharingInfo(false,true,0,"Ready",new String[]{"other","preferred"});}
    public static void main(String[] args){
        MouseSharingPolicy policy=new MouseSharingPolicy(0,"preferred");
        MouseSharingInfo empty=MouseSharingInfo.empty(),ready=ready();
        require(policy.update(27,empty,0).type==0,"Virtual never captures a mouse");
        policy.choose(1,"preferred");
        require(policy.update(-1,ready,0).type==0,"Choosing a side before screen creation only saves intent");
        require(policy.update(27,empty,0).type==MouseSharingPolicy.QUERY,"New screen refreshes mouse inventory");
        MouseSharingPolicy.Action start=policy.update(27,ready,10);
        require(start.type==MouseSharingPolicy.START&&start.side==1&&start.device.equals("preferred"),"Inventory completion automatically starts the saved mouse on the left");
        require(policy.update(27,ready,100).type==0,"Repeated capture publications do not duplicate a pending start");
        MouseSharingInfo active=new MouseSharingInfo(true,true,1,"Active",ready.devices);
        require(policy.update(27,active,200).type==0,"Successful start remains stable");
        MouseSharingInfo failed=ready();
        require(policy.update(27,failed,300).type==MouseSharingPolicy.QUERY,"Failure refreshes device identities before restarting");
        MouseSharingInfo reconnected=new MouseSharingInfo(false,true,0,"Ready",new String[]{"new identity"});
        require(policy.update(27,reconnected,400).device.equals("new identity"),"Reconnection uses an available identity instead of retrying a stale event path");
        require(policy.update(27,reconnected,500).type==0,"Only one restart is pending");
        policy.pause();
        require(policy.update(27,failed,10_000).type==0&&!policy.watching(),"Touchpad intent pauses automatic capture");
        require(policy.update(28,empty,10_100).type==MouseSharingPolicy.QUERY,"A fresh screen resumes saved placement");
        policy.choose(0,"");
        require(policy.update(28,ready,20_000).type==0,"Virtual cancels retries");
        policy.choose(2,"");
        require(policy.update(28,empty,30_000).type==MouseSharingPolicy.QUERY,"Explicit position change restarts discovery");
        require(policy.update(28,empty,30_100).type==0,"Missing replies are bounded by a timeout");
        require(policy.update(28,empty,35_000).type==MouseSharingPolicy.QUERY,"A timed-out request retries");
        System.out.println("PASS: placement intent, auto-start, failure/reconnect, pending fences, Touchpad pause and timeout retries");
    }
}
