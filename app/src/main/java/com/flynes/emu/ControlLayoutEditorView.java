package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;

import androidx.core.view.ViewCompat;
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat;
import androidx.customview.widget.ExploreByTouchHelper;

import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.ControlVisualGeometry;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;

import java.util.ArrayDeque;
import java.util.List;

final class ControlLayoutEditorView extends View {
    interface Listener{void onChanged();}
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG),stroke=new Paint(Paint.ANTI_ALIAS_FLAG),label=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayDeque<ControlLayoutV2> undo=new ArrayDeque<>();
    private ControlLayoutV2 layout=ControlLayoutV2.recommended();
    private ControlLayoutV2.Element selected=ControlLayoutV2.Element.A, pressed;
    private Listener listener; private boolean tryMode;
    private final AppSettings settings;
    private final EditorAccessibilityHelper accessibility;
    ControlLayoutEditorView(Context context){super(context);settings=new SettingsRepository(new SharedPreferencesSettingsStore(context)).load();setFocusable(true);setClickable(true);setContentDescription(context.getString(R.string.control_layout_hint));accessibility=new EditorAccessibilityHelper(this);ViewCompat.setAccessibilityDelegate(this,accessibility);}
    void setLayout(ControlLayoutV2 value){layout=value;invalidate();accessibility.invalidateRoot();}
    ControlLayoutV2 layout(){return layout;}
    void setListener(Listener value){listener=value;}
    float selectedScale(){return layout.placement(selected).scale();}
    int virtualControlCountForTest(){return 5;}
    CharSequence virtualControlNameForTest(int id){return accessibility.name(accessibility.order[id]);}
    boolean performVirtualControlClickForTest(int id){return accessibility.onPerformActionForVirtualView(id,AccessibilityNodeInfoCompat.ACTION_CLICK,null);}
    ControlLayoutV2.Element elementAtForTest(float x,float y){return elementAt(x,y);}
    boolean drawsJoystickForTest(){return getWidth()>0&&getHeight()>0&&map().joystickMode();}
    void setTryMode(boolean value){tryMode=value;pressed=null;invalidate();}
    void resizeSelected(float value){push();layout=layout.resize(selected,value);changed();}
    void setOpacity(float value){push();layout=layout.withOpacity(value);changed();}
    void undo(){if(!undo.isEmpty()){layout=undo.pop();changed();}}
    void resetRecommended(){push();layout=ControlLayoutV2.recommended();selected=ControlLayoutV2.Element.A;changed();}
    private void push(){if(undo.size()>=20)undo.removeLast();undo.push(layout);}
    private void changed(){invalidate();accessibility.invalidateRoot();if(listener!=null)listener.onChanged();}
    private GamepadHitMap map(){return GamepadHitMap.fromLayout(getWidth(),getHeight(),getResources().getDisplayMetrics().density,0,0,0,0,layout,settings.directionControlMode(),settings.deadZone());}
    @Override protected void onDraw(Canvas c){super.onDraw(c);drawTestFrame(c);if(getWidth()==0)return;GamepadHitMap m=map();if(m.joystickMode())drawJoystick(c,m);else drawDpad(c,m);draw(c,m.target(GamepadHitMap.Control.B),"B",ControlLayoutV2.Element.B);draw(c,m.target(GamepadHitMap.Control.A),"A",ControlLayoutV2.Element.A);draw(c,m.target(GamepadHitMap.Control.SELECT),"SELECT",ControlLayoutV2.Element.SELECT);draw(c,m.target(GamepadHitMap.Control.START),"START",ControlLayoutV2.Element.START);}
    private void drawTestFrame(Canvas c){c.drawColor(0xFF0D0E11);float h=getHeight(),w=Math.min(getWidth(),h*4f/3f),l=(getWidth()-w)/2f;paint.setColor(0xFF9BC4D7);c.drawRect(l,0,l+w,h,paint);paint.setColor(0xFF65708F);c.drawRect(l+w*.15f,h*.12f,l+w*.85f,h*.42f,paint);paint.setColor(0xFF34394B);for(int i=0;i<7;i++)c.drawRect(l+w*(.22f+i*.09f),h*.62f,l+w*(.27f+i*.09f),h,paint);paint.setColor(0xFFFF6B5E);c.drawRect(l+w*.47f,h*.58f,l+w*.53f,h,paint);}
    private void drawDpad(Canvas c,GamepadHitMap m){GamepadHitMap.Bounds d=m.dpadBounds();float arm=ControlVisualGeometry.dpadArmPx(getResources().getDisplayMetrics().density,layout.placement(ControlLayoutV2.Element.D_PAD).scale());style(ControlLayoutV2.Element.D_PAD);c.drawRoundRect(new RectF(d.centerX()-arm/2,d.top,d.centerX()+arm/2,d.bottom),12,12,paint);c.drawRoundRect(new RectF(d.left,d.centerY()-arm/2,d.right,d.centerY()+arm/2),12,12,paint);c.drawCircle(d.centerX(),d.centerY(),8,stroke);}
    private void drawJoystick(Canvas c,GamepadHitMap m){GamepadHitMap.Bounds d=m.dpadBounds();style(ControlLayoutV2.Element.D_PAD);float radius=d.width()/2f;c.drawCircle(d.centerX(),d.centerY(),radius,paint);c.drawCircle(d.centerX(),d.centerY(),radius,stroke);c.drawCircle(d.centerX(),d.centerY(),radius*.39f,paint);c.drawCircle(d.centerX(),d.centerY(),radius*.39f,stroke);}
    private void draw(Canvas c,GamepadHitMap.Target t,String text,ControlLayoutV2.Element element){style(element);RectF r=new RectF(t.left(),t.top(),t.right(),t.bottom());float radius=t.shape()==GamepadHitMap.Shape.CIRCLE?r.width()/2f:(t.shape()==GamepadHitMap.Shape.PILL?r.height()/2f:18f);c.drawRoundRect(r,radius,radius,paint);c.drawRoundRect(r,radius,radius,stroke);label.setColor(0xFFF4EFE6);label.setTextAlign(Paint.Align.CENTER);label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);label.setTextSize((t.shape()==GamepadHitMap.Shape.PILL?11:22)*getResources().getDisplayMetrics().density);Paint.FontMetrics f=label.getFontMetrics();c.drawText(text,r.centerX(),r.centerY()-(f.ascent+f.descent)/2,label);}
    private void style(ControlLayoutV2.Element e){boolean active=e==selected||e==pressed;paint.setStyle(Paint.Style.FILL);paint.setColor(active?0xDDFF6B5E:((Math.round(layout.opacity()*255)<<24)|0x25282F));stroke.setStyle(Paint.Style.STROKE);stroke.setStrokeWidth(active?6:3);stroke.setColor(active?0xFFFF6B5E:0xFFBEB8AE);}
    @Override public boolean onTouchEvent(MotionEvent e){if(getWidth()==0)return false;float x=e.getX(),y=e.getY();if(e.getActionMasked()==MotionEvent.ACTION_DOWN){selected=elementAt(x,y);if(selected==null)return false;if(tryMode){pressed=selected;invalidate();}else{push();}if(listener!=null)listener.onChanged();return true;}if(e.getActionMasked()==MotionEvent.ACTION_MOVE&&!tryMode&&selected!=null){layout=layout.move(selected,clamp(x/getWidth()),clamp(y/getHeight()));changed();return true;}if(e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_CANCEL){pressed=null;invalidate();performClick();return true;}return true;}
    private ControlLayoutV2.Element elementAt(float x,float y){GamepadHitMap m=map();GamepadHitMap.Control c=m.buttonHit(x,y);switch(c){case A:return ControlLayoutV2.Element.A;case B:return ControlLayoutV2.Element.B;case SELECT:return ControlLayoutV2.Element.SELECT;case START:return ControlLayoutV2.Element.START;default:break;}if(settings.directionControlMode()!=DirectionControlMode.JOYSTICK&&m.canStartDirection(x,y))return ControlLayoutV2.Element.D_PAD;c=m.hit(x,y);switch(c){case UP:case DOWN:case LEFT:case RIGHT:return ControlLayoutV2.Element.D_PAD;default:return null;}}
    private static float clamp(float v){return Math.max(.02f,Math.min(.98f,v));}
    @Override public boolean performClick(){super.performClick();return true;}

    private final class EditorAccessibilityHelper extends ExploreByTouchHelper {
        final ControlLayoutV2.Element[] order={ControlLayoutV2.Element.D_PAD,ControlLayoutV2.Element.A,
                ControlLayoutV2.Element.B,ControlLayoutV2.Element.SELECT,ControlLayoutV2.Element.START};
        EditorAccessibilityHelper(View host){super(host);}
        @Override protected int getVirtualViewAt(float x,float y){if(getWidth()<=0||getHeight()<=0)return INVALID_ID;ControlLayoutV2.Element hit=elementAt(x,y);if(hit==null)return INVALID_ID;for(int i=0;i<order.length;i++)if(order[i]==hit)return i;return INVALID_ID;}
        @Override protected void getVisibleVirtualViews(List<Integer> ids){for(int i=0;i<order.length;i++)ids.add(i);}
        @Override protected void onPopulateNodeForVirtualView(int id,AccessibilityNodeInfoCompat node){ControlLayoutV2.Element e=order[id];node.setClassName("android.widget.Button");node.setContentDescription(name(e));node.setClickable(true);node.addAction(AccessibilityNodeInfoCompat.ACTION_CLICK);if(getWidth()<=0||getHeight()<=0){node.setBoundsInParent(new Rect(0,0,1,1));return;}GamepadHitMap m=map();GamepadHitMap.Bounds b=e==ControlLayoutV2.Element.D_PAD?m.dpadBounds():m.target(control(e)).bounds();node.setBoundsInParent(new Rect(Math.round(b.left),Math.round(b.top),Math.round(b.right),Math.round(b.bottom)));}
        @Override protected boolean onPerformActionForVirtualView(int id,int action,Bundle args){if(action!=AccessibilityNodeInfoCompat.ACTION_CLICK)return false;selected=order[id];pressed=selected;changed();postDelayed(()->{pressed=null;invalidate();invalidateVirtualView(id);},80L);invalidateVirtualView(id);return true;}
        private GamepadHitMap.Control control(ControlLayoutV2.Element e){switch(e){case A:return GamepadHitMap.Control.A;case B:return GamepadHitMap.Control.B;case SELECT:return GamepadHitMap.Control.SELECT;default:return GamepadHitMap.Control.START;}}
        String name(ControlLayoutV2.Element e){switch(e){case D_PAD:return getContext().getString(settings.directionControlMode()!=DirectionControlMode.DPAD?R.string.control_joystick:R.string.control_dpad);case A:return getContext().getString(R.string.control_a);case B:return getContext().getString(R.string.control_b);case SELECT:return getContext().getString(R.string.control_select);default:return getContext().getString(R.string.control_start);}}
    }
}
