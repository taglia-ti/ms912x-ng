package io.github.taglia_ti.ms912x_ng.desktop;

/** UI value independent of the optional desktop IPC dependency. */
final class MouseSharingInfo {
    final boolean active,available;
    final int side;
    final String message;
    final String[] devices;
    MouseSharingInfo(boolean active,boolean available,int side,String message,String[] devices){
        this.active=active;this.available=available;this.side=side;this.message=message;this.devices=devices.clone();
    }
    static MouseSharingInfo empty(){return new MouseSharingInfo(false,false,2,"Create an independent screen to share a mouse.",new String[0]);}
    static String label(String identity){
        int separator=identity.indexOf('\t');
        String[] parts=(separator>=0?identity.substring(separator+1):identity).split(":",6);
        if(parts.length!=6)return "Mouse";
        try{return parts[5].trim()+String.format(java.util.Locale.ROOT," (%04x:%04x)",Integer.parseInt(parts[1]),Integer.parseInt(parts[2]));}
        catch(NumberFormatException invalid){return parts[5].trim();}
    }
}
