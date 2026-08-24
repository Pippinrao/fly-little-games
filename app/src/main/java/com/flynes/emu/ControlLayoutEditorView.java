package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.GamepadHitMap;

import java.util.ArrayDeque;

final class ControlLayoutEditorView extends View {
    interface Listener{void onChanged();}
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG),stroke=new Paint(Paint.ANTI_ALIAS_FLAG),label=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayDeque<ControlLayoutV2> undo=new ArrayDeque<>();
    private ControlLayoutV2 layout=ControlLayoutV2.recommended();
    private ControlLayoutV2.Element selected=ControlLayoutV2.Element.A, pressed;
    private Listener listener; private boolean tryMode;
    ControlLayoutEditorView(Context context){super(context);setFocusable(true);setClickable(true);setContentDescription(context.getString(R.string.control_layout_hint));}
    void setLayout(ControlLayoutV2 value){layout=value;invalidate();}
    ControlLayoutV2 layout(){return layout;}
    void setListener(Listener value){listener=value;}
    float selectedScale(){return layout.placement(selected).scale();}
    void setTryMode(boolean value){tryMode=value;pressed=null;invalidate();}
    void resizeSelected(float value){push();layout=layout.resize(selected,value);changed();}
    void setOpacity(float value){push();layout=layout.withOpacity(value);changed();}
    void undo(){if(!undo.isEmpty()){layout=undo.pop();changed();}}
    void resetRecommended(){push();layout=ControlLayoutV2.recommended();selected=ControlLayoutV2.Element.A;changed();}
    private void push(){if(undo.size()>=20)undo.removeLast();undo.push(layout);}
    private void changed(){invalidate();if(listener!=null)listener.onChanged();}
    private GamepadHitMap map(){return GamepadHitMap.fromLayout(getWidth(),getHeight(),getResources().getDisplayMetrics().density,0,0,0,0,layout);}
    @Override protected void onDraw(Canvas c){super.onDraw(c);drawTestFrame(c);if(getWidth()==0)return;GamepadHitMap m=map();drawDpad(c,m);draw(c,m.target(GamepadHitMap.Control.B),"B",ControlLayoutV2.Element.B);draw(c,m.target(GamepadHitMap.Control.A),"A",ControlLayoutV2.Element.A);draw(c,m.target(GamepadHitMap.Control.SELECT),"SELECT",ControlLayoutV2.Element.SELECT);draw(c,m.target(GamepadHitMap.Control.START),"START",ControlLayoutV2.Element.START);}
    private void drawTestFrame(Canvas c){c.drawColor(0xFF0D0E11);float h=getHeight(),w=Math.min(getWidth(),h*4f/3f),l=(getWidth()-w)/2f;paint.setColor(0xFF9BC4D7);c.drawRect(l,0,l+w,h,paint);paint.setColor(0xFF65708F);c.drawRect(l+w*.15f,h*.12f,l+w*.85f,h*.42f,paint);paint.setColor(0xFF34394B);for(int i=0;i<7;i++)c.drawRect(l+w*(.22f+i*.09f),h*.62f,l+w*(.27f+i*.09f),h,paint);paint.setColor(0xFFFF6B5E);c.drawRect(l+w*.47f,h*.58f,l+w*.53f,h,paint);}
    private void drawDpad(Canvas c,GamepadHitMap m){GamepadHitMap.Bounds d=m.dpadBounds();float arm=48f*getResources().getDisplayMetrics().density*layout.placement(ControlLayoutV2.Element.D_PAD).scale();style(ControlLayoutV2.Element.D_PAD);c.drawRoundRect(new RectF(d.centerX()-arm/2,d.top,d.centerX()+arm/2,d.bottom),12,12,paint);c.drawRoundRect(new RectF(d.left,d.centerY()-arm/2,d.right,d.centerY()+arm/2),12,12,paint);c.drawCircle(d.centerX(),d.centerY(),8,stroke);}
    private void draw(Canvas c,GamepadHitMap.Target t,String text,ControlLayoutV2.Element element){style(element);RectF r=new RectF(t.left(),t.top(),t.right(),t.bottom());float radius=t.shape()==GamepadHitMap.Shape.CIRCLE?r.width()/2f:(t.shape()==GamepadHitMap.Shape.PILL?r.height()/2f:18f);c.drawRoundRect(r,radius,radius,paint);c.drawRoundRect(r,radius,radius,stroke);label.setColor(0xFFF4EFE6);label.setTextAlign(Paint.Align.CENTER);label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);label.setTextSize((t.shape()==GamepadHitMap.Shape.PILL?11:22)*getResources().getDisplayMetrics().density);Paint.FontMetrics f=label.getFontMetrics();c.drawText(text,r.centerX(),r.centerY()-(f.ascent+f.descent)/2,label);}
    private void style(ControlLayoutV2.Element e){boolean active=e==selected||e==pressed;paint.setStyle(Paint.Style.FILL);paint.setColor(active?0xDDFF6B5E:((Math.round(layout.opacity()*255)<<24)|0x25282F));stroke.setStyle(Paint.Style.STROKE);stroke.setStrokeWidth(active?6:3);stroke.setColor(active?0xFFFF6B5E:0xFFBEB8AE);}
    @Override public boolean onTouchEvent(MotionEvent e){if(getWidth()==0)return false;float x=e.getX(),y=e.getY();if(e.getActionMasked()==MotionEvent.ACTION_DOWN){selected=elementAt(x,y);if(selected==null)return false;if(tryMode){pressed=selected;invalidate();}else{push();}if(listener!=null)listener.onChanged();return true;}if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&!tryMode&&selected!=null){layout=layout.move(selected,clamp(x/getWidth()),clamp(y/getHeight()));changed();return true;}if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){pressed=null;invalidate();performClick();return true;}return true;}
    private ControlLayoutV2.Element elementAt(float x,float y){GamepadHitMap m=map();GamepadHitMap.Control c=m.hit(x,y);switch(c){case UP:case DOWN:case LEFT:case RIGHT:return ControlLayoutV2.Element.D_PAD;case A:return ControlLayoutV2.Element.A;case B:return ControlLayoutV2.Element.B;case SELECT:return ControlLayoutV2.Element.SELECT;case START:return ControlLayoutV2.Element.START;default:return null;}}
    private static float clamp(float v){return Math.max(.02f,Math.min(.98f,v));}
    @Override public boolean performClick(){super.performClick();return true;}
}
