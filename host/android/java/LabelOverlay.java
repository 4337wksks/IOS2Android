package local.ios.host;

import android.content.Context;
import android.view.View;
import android.widget.FrameLayout;
import java.util.IdentityHashMap;

class LabelOverlay extends FrameLayout {
    static void detach(View child) {
        if(child.getParent() instanceof android.view.ViewGroup)((android.view.ViewGroup)child.getParent()).removeView(child);
    }
    static void attach(LabelOverlay parent,View child) {
        if(child.getParent()==parent)return;
        detach(child);parent.addView(child);
    }
    static void updateVisibility(View child,boolean visible) {
        if(child.getParent() instanceof LabelOverlay)((LabelOverlay)child.getParent()).visibility(child,visible);
    }
    private static final class Entry {
        final View view;
        float depth, windowLevel;
        int childIndex, position;
        boolean visible;
        Entry(View view) { this.view=view; }
    }
    private final IdentityHashMap<View,Entry> entries=new IdentityHashMap<>();
    private Entry[] order;
    private boolean dirty;
    private int visibleCount;
    private final int[] windowLocation=new int[2];
    void visibility(View child,boolean visible) {
        Entry entry=entries.get(child);
        if (entry==null || entry.visible==visible) return;
        boolean hadVisible=visibleCount!=0;
        entry.visible=visible;
        visibleCount+=visible ? 1 : -1;
        if (hadVisible!=(visibleCount!=0) && getParent()!=null)
            getParent().requestTransparentRegion(this);
    }
    @Override public boolean gatherTransparentRegion(android.graphics.Region region) {

        if (visibleCount!=0 && getVisibility()==View.VISIBLE) {
            if (region==null) return false;
            getLocationInWindow(windowLocation);
            region.op(windowLocation[0],windowLocation[1],windowLocation[0]+getWidth(),
                    windowLocation[1]+getHeight(),android.graphics.Region.Op.DIFFERENCE);
            return false;
        }
        return true;
    }

    LabelOverlay(Context context) {
        super(context);
        setChildrenDrawingOrderEnabled(true);
        setClipChildren(false); setClipToPadding(false);
    }
    @Override public void onViewAdded(View child) {
        super.onViewAdded(child);
        entries.put(child,new Entry(child)); order=null; dirty=true;
    }
    @Override public void onViewRemoved(View child) {
        super.onViewRemoved(child);
        Entry entry=entries.remove(child);
        if (entry!=null && entry.visible && --visibleCount==0 && getParent()!=null)getParent().requestTransparentRegion(this);
        order=null; dirty=true;
    }
    @Override public void bringChildToFront(View child) {
        super.bringChildToFront(child); order=null; dirty=true;
    }
    private static int compare(Entry first,Entry second) {
        int window=Float.compare(first.windowLevel,second.windowLevel);
        if (window!=0) return window;
        int depth=Float.compare(first.depth,second.depth);
        return depth!=0 ? depth : Integer.compare(first.childIndex,second.childIndex);
    }
    void depth(View child,float value,float windowLevel) {
        Entry entry=entries.get(child);
        if (entry==null || entry.depth==value && entry.windowLevel==windowLevel) return;
        entry.depth=value; entry.windowLevel=windowLevel;
        if (order==null || dirty) { dirty=true; invalidate(); return; }
        int position=entry.position;
        if ((position>0 && compare(order[position-1],entry)>0) ||
                (position+1<order.length && compare(entry,order[position+1])>0)) {
            dirty=true; invalidate();
        }
    }
    @Override protected int getChildDrawingOrder(int count,int drawingPosition) {
        if (order==null || order.length!=count) {
            order=new Entry[count];
            for (int i=0;i<count;++i) {
                Entry entry=entries.get(getChildAt(i)); entry.childIndex=i; order[i]=entry;
            }
            dirty=true;
        }
        if (dirty) {

            for (int i=1;i<count;++i) {
                Entry entry=order[i]; int j=i;
                while (j>0 && compare(order[j-1],entry)>0) { order[j]=order[j-1]; --j; }
                order[j]=entry;
            }
            for (int i=0;i<count;++i) order[i].position=i;
            dirty=false;
        }
        return order[drawingPosition].childIndex;
    }
}
