package local.ios.host;

/** Presentation supplied by an application adapter; callbacks run on the UI thread. */
interface GuestModal {
    void show(boolean animated,Runnable completion);
    void dismiss(boolean animated,Runnable completion);
    void actionEnabled(int index,boolean enabled);
}
