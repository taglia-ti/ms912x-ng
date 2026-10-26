package io.github.taglia_ti.ms912x_ng.helper;

import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES11Ext;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/** Optional helper-side GPU transform. No pixel readback or CPU conversion occurs here. */
final class FrameRenderer implements AutoCloseable {
    private final HandlerThread thread = new HandlerThread("ms912x-helper-gl");
    private final Handler handler;
    private final CaptureTransform transform;
    private EGLDisplay display = EGL14.EGL_NO_DISPLAY;
    private EGLContext context = EGL14.EGL_NO_CONTEXT;
    private EGLSurface window = EGL14.EGL_NO_SURFACE;
    private SurfaceTexture texture;
    private Surface input;
    private int program, textureId, vertexShader, fragmentShader;
    private int positionLocation, uvLocation, matrixLocation, samplerLocation;
    private FloatBuffer positions, uv;
    private Rect bounds;
    private final float[] textureMatrix = new float[16];
    private volatile RuntimeException failed;
    private boolean closed;

    FrameRenderer(Surface output, CaptureTransform transform, int width, int height) throws Exception {
        this.transform = transform;
        thread.start();
        handler = new Handler(thread.getLooper());
        try {
            call(() -> { initialize(output, width, height); return null; });
        } catch (Exception failure) {
            handler.post(() -> { cleanup(); thread.quitSafely(); });
            throw failure;
        }
    }

    Surface input() { return input; }

    void resize(int width, int height) throws Exception {
        healthy();
        call(() -> {
            texture.setDefaultBufferSize(width, height);
            bounds = transform.bounds(width, height);
            return null;
        });
    }

    void healthy() { if (failed != null) throw failed; }

    private <T> T call(Callable<T> action) throws Exception {
        FutureTask<T> task = new FutureTask<>(action);
        if (!handler.post(task)) throw new IllegalStateException("Renderer stopped");
        return task.get(3, TimeUnit.SECONDS);
    }

    private void initialize(Surface output, int width, int height) {
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        int[] version = new int[2];
        require(display != EGL14.EGL_NO_DISPLAY && EGL14.eglInitialize(display, version, 0, version, 1), "EGL initialization");
        EGLConfig[] configs = new EGLConfig[1]; int[] count = new int[1];
        int[] attributes = {EGL14.EGL_RED_SIZE,8,EGL14.EGL_GREEN_SIZE,8,EGL14.EGL_BLUE_SIZE,8,
                EGL14.EGL_ALPHA_SIZE,8,EGL14.EGL_RENDERABLE_TYPE,EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_SURFACE_TYPE,EGL14.EGL_WINDOW_BIT,EGL14.EGL_NONE};
        require(EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) && count[0]>0, "EGL configuration");
        context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION,2,EGL14.EGL_NONE},0);
        require(context != EGL14.EGL_NO_CONTEXT, "EGL context");
        window = EGL14.eglCreateWindowSurface(display, configs[0], output, new int[]{EGL14.EGL_NONE},0);
        require(window != EGL14.EGL_NO_SURFACE && EGL14.eglMakeCurrent(display,window,window,context), "EGL output surface");
        vertexShader = shader(GLES20.GL_VERTEX_SHADER,
                "attribute vec2 position; attribute vec2 uv; uniform mat4 textureMatrix; varying vec2 sampleUv;"
                        + "void main(){ gl_Position=vec4(position,0.0,1.0); sampleUv=(textureMatrix*vec4(uv,0.0,1.0)).xy; }");
        int[] precision = new int[1];
        GLES20.glGetShaderPrecisionFormat(GLES20.GL_FRAGMENT_SHADER, GLES20.GL_HIGH_FLOAT, new int[2], 0, precision, 0);
        fragmentShader = shader(GLES20.GL_FRAGMENT_SHADER,
                "#extension GL_OES_EGL_image_external : require\nprecision " + (precision[0] > 0 ? "highp" : "mediump") + " float;"
                        + "uniform samplerExternalOES image; varying vec2 sampleUv;"
                        + "void main(){gl_FragColor=texture2D(image,sampleUv);}");
        program = GLES20.glCreateProgram();
        GLES20.glAttachShader(program,vertexShader); GLES20.glAttachShader(program,fragmentShader);
        GLES20.glLinkProgram(program);
        int[] status = new int[1]; GLES20.glGetProgramiv(program,GLES20.GL_LINK_STATUS,status,0);
        require(status[0] != 0, "GL program linking");
        positionLocation = GLES20.glGetAttribLocation(program,"position");
        uvLocation = GLES20.glGetAttribLocation(program,"uv");
        matrixLocation = GLES20.glGetUniformLocation(program,"textureMatrix");
        samplerLocation = GLES20.glGetUniformLocation(program,"image");
        float[] xy = {-1,-1,1,-1,-1,1,1,1};
        for (int i=0;i<xy.length;i+=2) {
            float x=xy[i],y=xy[i+1];
            if(transform.rotation==1){xy[i]=y;xy[i+1]=-x;}
            if(transform.rotation==2){xy[i]=-x;xy[i+1]=-y;}
            if(transform.rotation==3){xy[i]=-y;xy[i+1]=x;}
        }
        positions = floats(xy); uv = floats(new float[]{0,0,1,0,0,1,1,1});
        int[] ids = new int[1]; GLES20.glGenTextures(1,ids,0); textureId=ids[0];
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,textureId);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,GLES20.GL_TEXTURE_MIN_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,GLES20.GL_TEXTURE_MAG_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,GLES20.GL_TEXTURE_WRAP_S,GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,GLES20.GL_TEXTURE_WRAP_T,GLES20.GL_CLAMP_TO_EDGE);
        texture = new SurfaceTexture(textureId);
        texture.setDefaultBufferSize(width,height);
        bounds = transform.bounds(width,height);
        input = new Surface(texture);
        texture.setOnFrameAvailableListener(ignored -> {
            if (closed || failed != null) return;
            try { draw(); }
            catch(RuntimeException failure){failed=failure;texture.setOnFrameAvailableListener(null);}
        },handler);
        require(GLES20.glGetError()==GLES20.GL_NO_ERROR,"GL initialization");
    }

    private void draw() {
        texture.updateTexImage(); texture.getTransformMatrix(textureMatrix);
        GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
        GLES20.glDisable(GLES20.GL_BLEND);
        GLES20.glClearColor(0,0,0,1); GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glViewport(bounds.left,transform.outputHeight-bounds.bottom,bounds.width(),bounds.height());
        GLES20.glUseProgram(program);
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,textureId);
        GLES20.glUniform1i(samplerLocation,0);
        GLES20.glUniformMatrix4fv(matrixLocation,1,false,textureMatrix,0);
        GLES20.glEnableVertexAttribArray(positionLocation); GLES20.glEnableVertexAttribArray(uvLocation);
        GLES20.glVertexAttribPointer(positionLocation,2,GLES20.GL_FLOAT,false,0,positions);
        GLES20.glVertexAttribPointer(uvLocation,2,GLES20.GL_FLOAT,false,0,uv);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP,0,4);
        require(GLES20.glGetError()==GLES20.GL_NO_ERROR && EGL14.eglSwapBuffers(display,window),"GL frame submission");
    }

    private static FloatBuffer floats(float[] values) {
        FloatBuffer buffer=ByteBuffer.allocateDirect(values.length*4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        buffer.put(values).position(0); return buffer;
    }
    private static int shader(int type,String source) {
        int shader=GLES20.glCreateShader(type); GLES20.glShaderSource(shader,source); GLES20.glCompileShader(shader);
        int[] status=new int[1]; GLES20.glGetShaderiv(shader,GLES20.GL_COMPILE_STATUS,status,0);
        if(status[0]==0){GLES20.glDeleteShader(shader);throw new IllegalStateException("GL shader compilation");}
        return shader;
    }
    private static void require(boolean condition,String operation) {
        if(!condition) throw new IllegalStateException(operation+" failed");
    }

    private void cleanup() {
        closed=true;
        if(texture!=null) texture.setOnFrameAvailableListener(null);
        if(input!=null) input.release();
        if(texture!=null) texture.release();
        if(program!=0) GLES20.glDeleteProgram(program);
        if(vertexShader!=0) GLES20.glDeleteShader(vertexShader);
        if(fragmentShader!=0) GLES20.glDeleteShader(fragmentShader);
        if(textureId!=0) GLES20.glDeleteTextures(1,new int[]{textureId},0);
        if(display!=EGL14.EGL_NO_DISPLAY) {
            EGL14.eglMakeCurrent(display,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_CONTEXT);
            if(window!=EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display,window);
            if(context!=EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display,context);
            EGL14.eglReleaseThread(); EGL14.eglTerminate(display);
        }
    }

    @Override public void close() {
        try { call(() -> { cleanup(); return null; }); }
        catch(Exception failure){throw new IllegalStateException("Renderer release failed",failure);}
        finally {thread.quitSafely();}
    }
}
