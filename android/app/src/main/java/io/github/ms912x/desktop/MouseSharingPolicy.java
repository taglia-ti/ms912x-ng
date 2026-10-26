package io.github.ms912x.desktop;

/** Service-owned intent and retry timing; independent of the Activity and Android timers. */
final class MouseSharingPolicy {
    static final int NONE=0,QUERY=1,START=2;
    static final class Action {
        final int type,side;final String device;
        Action(int type,int side,String device){this.type=type;this.side=side;this.device=device;}
    }
    private int side,display=-1,attempts,lastCommand;
    private String preferred;
    private boolean paused,pending,apply,queryFirst;
    private long next,deadline;
    private MouseSharingInfo response;
    MouseSharingPolicy(int side,String preferred){choose(side,preferred);}
    int side(){return side;}
    void choose(int value,String device){
        if(value<0||value>4)throw new IllegalArgumentException("Invalid screen placement");
        side=value;preferred=device==null?"":device;paused=false;pending=false;apply=true;queryFirst=true;attempts=0;next=0;
    }
    void pause(){paused=true;pending=false;}
    boolean watching(){return side>0&&display>0&&!paused;}
    Action update(int createdDisplay,MouseSharingInfo state,long now){
        if(display!=createdDisplay){display=createdDisplay;pending=false;paused=false;attempts=0;next=0;apply=true;queryFirst=true;}
        if(!watching())return new Action(NONE,side,"");
        if(pending){
            if(response!=state){pending=false;if(lastCommand==QUERY&&state.available&&state.devices.length>0)next=0;}
            else if(now<deadline)return new Action(NONE,side,"");
            else pending=false;
        }
        if(state.active&&!apply&&state.side==side){attempts=0;next=0;queryFirst=true;return new Action(NONE,side,"");}
        if(now<next)return new Action(NONE,side,"");
        int command=queryFirst&&!state.active||!state.available||state.devices.length==0?QUERY:START;
        String device=state.devices.length==0?"":state.devices[0];
        for(String candidate:state.devices)if(candidate.equals(preferred))device=candidate;
        response=state;pending=true;deadline=now+5_000;
        lastCommand=command;
        next=now+Math.min(10_000,1_000L<<Math.min(4,attempts++));
        queryFirst=command==START;
        if(command==START)apply=false;
        return new Action(command,side,device);
    }
    MouseSharingInfo describe(MouseSharingInfo state){
        if(display<=0||side==0||state.active)return state;
        return new MouseSharingInfo(false,state.available,state.side,
                paused?"Mouse sharing paused for Touchpad. Choose Start mouse sharing to resume.":
                        state.message+" Retrying mouse sharing automatically.",state.devices);
    }
}
