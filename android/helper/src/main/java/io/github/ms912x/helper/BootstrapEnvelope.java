package io.github.ms912x.helper;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** Bounded ASCII framing, without buffered read-ahead into the retained lifetime stream. */
final class BootstrapEnvelope {
    final long session;
    final int appUid, protocol;
    final String authority, digest, token;

    private BootstrapEnvelope(String[] fields) throws IOException {
        try {
            session=Long.parseLong(fields[1]);appUid=Integer.parseInt(fields[2]);protocol=Integer.parseInt(fields[3]);
        } catch(NumberFormatException invalid) { throw new IOException("Invalid bootstrap identity"); }
        authority=fields[4];digest=fields[5];token=fields[6];
        if(!fields[0].equals("MS912X_BOOTSTRAP_1")||session<=0||appUid<10000||protocol<=0
                ||!authority.matches("[a-zA-Z0-9_.]+\\.helper")
                ||!digest.matches("[0-9a-f]{64}")||!token.matches("[0-9a-f]{64}"))
            throw new IOException("Invalid bootstrap envelope");
    }
    static BootstrapEnvelope read(InputStream input) throws IOException {
        String[] fields=new String[7];int total=0;
        for(int i=0;i<fields.length;++i){
            java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();
            while(true){
                int value=input.read();
                if(value<0||++total>2048)throw new IOException("Incomplete or oversized bootstrap envelope");
                if(value=='\n')break;
                if(value<32||value>126||bytes.size()>=256)throw new IOException("Invalid bootstrap field");
                bytes.write(value);
            }
            fields[i]=new String(bytes.toByteArray(),StandardCharsets.US_ASCII);
        }
        return new BootstrapEnvelope(fields);
    }
}
