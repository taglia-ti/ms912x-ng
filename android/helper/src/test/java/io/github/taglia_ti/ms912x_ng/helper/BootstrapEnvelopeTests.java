package io.github.taglia_ti.ms912x_ng.helper;

import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;

/** Secret-safe parser checks, including preservation of the retained stdin tail. */
public final class BootstrapEnvelopeTests {
    private static final String DIGEST="a".repeat(64),TOKEN="b".repeat(64);
    private static String valid(){return "MS912X_BOOTSTRAP_1\n42\n10001\n3\nio.github.taglia_ti.ms912x_ng.desktop.helper\n"+DIGEST+"\n"+TOKEN+"\n";}
    private static void rejected(String value) throws Exception {
        try{BootstrapEnvelope.read(new ByteArrayInputStream(value.getBytes(StandardCharsets.US_ASCII)));}
        catch(java.io.IOException|IllegalArgumentException expected){
            if(expected.getMessage().contains(TOKEN))throw new AssertionError("Secret entered parser diagnostics");return;
        }
        throw new AssertionError("Malformed bootstrap was accepted");
    }
    public static void main(String[] args) throws Exception {
        ByteArrayInputStream input=new ByteArrayInputStream((valid()+"lifetime").getBytes(StandardCharsets.US_ASCII));
        BootstrapEnvelope envelope=BootstrapEnvelope.read(input);
        if(envelope.session!=42||envelope.appUid!=10001||!envelope.token.equals(TOKEN)||input.read()!='l')throw new AssertionError("Envelope or retained stdin was changed");
        rejected(valid().replace("BOOTSTRAP_1","BOOTSTRAP_2"));
        rejected(valid().replace("\n42\n","\n0\n"));
        rejected(valid().replace("\n10001\n","\n2000\n"));
        rejected(valid().replace("desktop.helper","desktop;unsafe.helper"));
        rejected(valid().replace(DIGEST,"invalid"));
        rejected(valid().replace(TOKEN,"short"));
        rejected(valid().substring(0,valid().length()-1));
        rejected("x".repeat(2049));
        rejected("\r"+valid());
        System.out.println("PASS: bootstrap envelope validation and exact stdin consumption");
    }
}
