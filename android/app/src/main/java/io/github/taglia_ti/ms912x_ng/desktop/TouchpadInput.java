package io.github.taglia_ti.ms912x_ng.desktop;

/** App-side gesture actions; the Desktop build translates these to the ADB wire protocol. */
final class TouchpadInput {
    static final int MOVE=1,CLICK=2,SCROLL=3,DRAG_START=4,DRAG_END=5,BACK=6,HOME=7,CANCEL=8;
    private TouchpadInput(){}
}
