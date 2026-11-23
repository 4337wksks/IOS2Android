package local.ios.host;

import android.os.Handler;
import android.util.Log;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.UnknownHostException;
import java.net.SocketTimeoutException;
import java.net.ConnectException;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

final class HttpTransport {
    interface FailureSink { void report(String message); }
    private static native String[] requestInfo(long token);
    private static native int readBody(long token, byte[] buffer);
    private static native int redirect(long token, String url, int status, String[] headers, String target);
    private static native int response(long token, String url, int status, String[] headers, long length, String mime, String encoding);
    private static native long[] settings(long token);
    private static native void data(long token, byte[] bytes, int length);
    private static native void sentBody(long token, long count, long total, long expected);
    private static native void complete(long token, int error, String description);
    private static native void authenticate(long token, String host, int port, X509Certificate[] chain);
    private static native boolean dispatchCallback(long token, Runnable runnable);

    private final Handler main;
    private final ExecutorService workers;
    private final FailureSink failures;
    private final ConcurrentHashMap<Long, Job> jobs = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, java.util.concurrent.Semaphore> hostLimits = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Long, CookieJar> cookieStores = new ConcurrentHashMap<>();
    private static final class CookieJar {
        final java.net.CookieManager manager=new java.net.CookieManager(null,java.net.CookiePolicy.ACCEPT_ORIGINAL_SERVER);
        final Map<String,Long> expires=new java.util.HashMap<>();
        volatile int policy=2;
        private static String key(java.net.HttpCookie cookie) { return cookie.getDomain()+"\n"+cookie.getPath()+"\n"+cookie.getName(); }
        private void remember(java.net.HttpCookie cookie) {
            long age=cookie.getMaxAge(),now=System.currentTimeMillis();
            if(age<0)expires.remove(key(cookie));
            else expires.put(key(cookie),age>(Long.MAX_VALUE-now)/1000 ? Long.MAX_VALUE : now+age*1000);
        }
        synchronized void put(java.net.URI uri,Map<String,List<String>> headers,int acceptance) throws IOException {
            if(acceptance==1 || policy==1)return;
            manager.setCookiePolicy(acceptance==0 && policy==0 ? java.net.CookiePolicy.ACCEPT_ALL : java.net.CookiePolicy.ACCEPT_ORIGINAL_SERVER);
            manager.put(uri,headers);
            for(Map.Entry<String,List<String>> entry:headers.entrySet())if(entry.getKey()!=null && (entry.getKey().equalsIgnoreCase("Set-Cookie") || entry.getKey().equalsIgnoreCase("Set-Cookie2")))
                for(String header:entry.getValue())try{for(java.net.HttpCookie cookie:java.net.HttpCookie.parse(header)){
                    if(cookie.getDomain()==null)cookie.setDomain(uri.getHost());
                    if(cookie.getPath()==null){String path=uri.getPath();int slash=path==null ? -1 : path.lastIndexOf('/');cookie.setPath(slash>0 ? path.substring(0,slash) : "/");}
                    remember(cookie);
                }}catch(IllegalArgumentException ignored){}
        }
        synchronized org.json.JSONArray snapshot(java.net.URI uri) throws org.json.JSONException {
            org.json.JSONArray result=new org.json.JSONArray();java.util.Set<String> active=new java.util.HashSet<>();
            for(java.net.HttpCookie cookie:manager.getCookieStore().getCookies()){
                active.add(key(cookie));
                if(cookie.hasExpired())continue;
                if(uri!=null){String path=uri.getPath();if(path==null || path.isEmpty())path="/";String root=cookie.getPath()==null ? "/" : cookie.getPath();
                    String domain=cookie.getDomain(),host=uri.getHost();if(domain==null || host==null)continue;
                    if(!host.equalsIgnoreCase(domain) && !java.net.HttpCookie.domainMatches(domain,host))continue;
                    if(!path.startsWith(root) || path.length()>root.length() && !root.endsWith("/") && path.charAt(root.length())!='/')continue;
                    if(cookie.getSecure() && !"https".equalsIgnoreCase(uri.getScheme()))continue;
                }
                org.json.JSONObject properties=new org.json.JSONObject();
                properties.put("Name",cookie.getName());properties.put("Value",cookie.getValue());properties.put("Domain",cookie.getDomain());properties.put("Path",cookie.getPath()==null ? "/" : cookie.getPath());
                properties.put("Secure",cookie.getSecure());properties.put("HttpOnly",cookie.isHttpOnly());properties.put("Version",cookie.getVersion());
                Long expiration=expires.get(key(cookie));if(expiration!=null)properties.put("Expires",expiration/1000.0);
                result.put(properties);
            }
            expires.keySet().retainAll(active);return result;
        }
        synchronized void set(org.json.JSONObject properties,boolean remove) throws Exception {
            java.net.HttpCookie cookie=new java.net.HttpCookie(properties.getString("Name"),properties.getString("Value"));
            cookie.setDomain(properties.getString("Domain"));cookie.setPath(properties.optString("Path","/"));
            java.net.URI uri=new java.net.URI((properties.optBoolean("Secure") ? "https" : "http")+"://"+cookie.getDomain().replaceFirst("^\\.",""));
            if(remove){manager.getCookieStore().remove(uri,cookie);expires.remove(key(cookie));return;}
            if(policy==1)return;
            cookie.setVersion(properties.optInt("Version",0));cookie.setSecure(properties.optBoolean("Secure"));cookie.setHttpOnly(properties.optBoolean("HttpOnly"));
            if(properties.has("Expires")){long expiration=(long)(properties.getDouble("Expires")*1000);expires.put(key(cookie),expiration);cookie.setMaxAge(Math.max(0,(expiration-System.currentTimeMillis()+999)/1000));}
            else expires.remove(key(cookie));
            manager.getCookieStore().add(uri,cookie);
        }
    }
    String cookieCommand(long token,String operation,String json) throws Exception {
        CookieJar jar=cookieStores.computeIfAbsent(token,ignored->new CookieJar());
        org.json.JSONObject input=new org.json.JSONObject(json);
        if(operation.equals("cookies") || operation.equals("url"))return jar.snapshot(operation.equals("url") ? new java.net.URI(input.getString("url")) : null).toString();
        if(operation.equals("policy")){jar.policy=input.getInt("policy");return "{}";}
        if(operation.equals("set") || operation.equals("delete")){jar.set(input,operation.equals("delete"));return "{}";}
        if(operation.equals("response")){
            Map<String,List<String>> headers=new java.util.HashMap<>();org.json.JSONObject fields=input.getJSONObject("headers");
            java.util.Iterator<String> keys=fields.keys();while(keys.hasNext()){String key=keys.next();headers.put(key,java.util.Collections.singletonList(fields.getString(key)));}
            jar.put(new java.net.URI(input.getString("url")),headers,jar.policy);return "{}";
        }
        throw new IllegalArgumentException("Unknown cookie operation");
    }
    private static final class Cancelled extends IOException {}
    private static final class TransportError extends IOException {
        final int code;
        TransportError(int code, String message) { super(message); this.code = code; }
    }
    private static final class Job {
        final long token;
        final boolean synchronous;
        volatile boolean cancelled;
        volatile HttpURLConnection connection;
        volatile CompletableFuture<Integer> authentication;
        volatile CompletableFuture<Integer> decision;
        volatile boolean suspended;
        volatile boolean resourceExpired;
        volatile FutureTask<?> callback;
        Thread permitWaiter;
        long[] settings;
        int timeout = 60_000;
        Job(long token) { this(token, false); }
        Job(long token, boolean synchronous) { this.token = token; this.synchronous = synchronous; }
    }

    HttpTransport(Handler main, ExecutorService workers, FailureSink failures) {
        this.main = main; this.workers = workers; this.failures = failures;
    }

    void start(long token) {
        Job job = new Job(token);
        if (jobs.putIfAbsent(token, job) != null) return;
        workers.execute(() -> run(job));
    }

    void sendSynchronously(long token) {
        Job job = new Job(token, true);
        if (jobs.putIfAbsent(token, job) != null) throw new IllegalStateException("Duplicate synchronous connection");
        job.settings=settings(token);
        FutureTask<Void> task = new FutureTask<>(() -> { run(job); return null; });
        try { workers.execute(task); }
        catch (RuntimeException failure) { jobs.remove(token, job); throw failure; }
        boolean interrupted = false;
        boolean timed=job.settings[2]>0;
        long deadline=timed ? System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(Math.min(job.settings[2],Long.MAX_VALUE/1_000_000)) : 0;
        try {
            for (;;) {
                try { if(timed)task.get(Math.max(1,deadline-System.nanoTime()),TimeUnit.NANOSECONDS);else task.get();return; }
                catch(TimeoutException timeout){expire(job);timed=false;}
                catch (InterruptedException waiting) { interrupted = true; }
                catch (ExecutionException failure) { throw new IllegalStateException("Synchronous connection failed", failure.getCause()); }
            }
        } finally {

            if (interrupted) Thread.currentThread().interrupt();
        }
    }
    private void expire(Job job) {
        job.resourceExpired=true;
        FutureTask<?> callback=job.callback;if(callback!=null)callback.cancel(false);
        synchronized(job){job.suspended=false;if(job.permitWaiter!=null)job.permitWaiter.interrupt();job.notifyAll();}
        SocketTimeoutException failure=new SocketTimeoutException("URLSession resource timeout");
        if(job.decision!=null)job.decision.completeExceptionally(failure);
        if(job.authentication!=null)job.authentication.completeExceptionally(failure);
        HttpURLConnection connection=job.connection;if(connection!=null)workers.execute(connection::disconnect);
    }

    void cancel(long token) {
        Job job = jobs.get(token);
        if (job == null) return;
        job.cancelled = true;
        CompletableFuture<Integer> decision = job.authentication;
        if (decision != null) decision.complete(2);
        if (job.decision != null) job.decision.complete(0);
        FutureTask<?> callback=job.callback;if(callback!=null)callback.cancel(false);
        synchronized (job) { if(job.permitWaiter!=null)job.permitWaiter.interrupt();job.notifyAll(); }
        HttpURLConnection connection = job.connection;
        if (connection != null) workers.execute(connection::disconnect);
    }

    void resolveAuthentication(long token, int decision) {
        Job job = jobs.get(token);
        if (job != null && job.authentication != null) job.authentication.complete(decision);
    }
    void resolveDecision(long token, int decision) {
        Job job = jobs.get(token);
        if (job != null && job.decision != null) job.decision.complete(decision);
    }
    void suspend(long token, boolean suspended) {
        Job job = jobs.get(token);
        if (job != null) synchronized (job) { job.suspended = suspended; job.notifyAll(); }
    }
    private void awaitRunning(Job job) throws Exception {
        synchronized (job) { while (job.suspended && !job.cancelled) job.wait(); }
        if (job.cancelled) throw new Cancelled();
        if (job.resourceExpired) throw new SocketTimeoutException("URLSession resource timeout");
    }
    private boolean decision(Job job, Callable<Integer> callback) throws Exception {
        job.decision = new CompletableFuture<>();
        try {
            int result = onMain(job, callback);
            if (result == 2) result = job.decision.get(job.timeout, TimeUnit.MILLISECONDS);
            return result == 1;
        } finally { job.decision = null; }
    }

    private <T> T onMain(Job job, Callable<T> action) throws Exception {
        awaitRunning(job);
        if (job.synchronous) return action.call();
        FutureTask<T> task = new FutureTask<>(action);
        job.callback=task;
        try {
            awaitRunning(job);
            if (!dispatchCallback(job.token,task) && !main.post(task)) throw new IOException("Android main Looper is unavailable");
            return task.get(job.timeout,TimeUnit.MILLISECONDS);
        } catch (TimeoutException waiting) {
            throw new SocketTimeoutException("Waiting for connection delegate");
        } catch (java.util.concurrent.CancellationException cancelled) {
            awaitRunning(job);throw new IOException("Connection callback cancelled");
        } catch (ExecutionException failure) {
                Throwable cause = failure.getCause();
                if (cause instanceof Exception) throw (Exception) cause;
                if (cause instanceof Error) throw (Error) cause;
                throw new IOException(cause);
        } finally { job.callback=null;task.cancel(false); }
    }

    private static X509TrustManager systemTrust() throws Exception {
        TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        factory.init((KeyStore) null);
        for (TrustManager manager : factory.getTrustManagers())
            if (manager instanceof X509TrustManager) return (X509TrustManager) manager;
        throw new CertificateException("Android system trust manager is unavailable");
    }

    private void configureTLS(Job job, HttpsURLConnection connection) throws Exception {
        X509TrustManager system = systemTrust();
        X509TrustManager bridge = new X509TrustManager() {
            @Override public X509Certificate[] getAcceptedIssuers() { return system.getAcceptedIssuers(); }
            @Override public void checkClientTrusted(X509Certificate[] chain, String type) throws CertificateException {
                system.checkClientTrusted(chain, type);
            }
            @Override public void checkServerTrusted(X509Certificate[] chain, String type) throws CertificateException {
                CompletableFuture<Integer> decision = new CompletableFuture<>();
                job.authentication = decision;
                try {
                    URL url = connection.getURL();
                    onMain(job, () -> { authenticate(job.token, url.getHost(), url.getPort() < 0 ? 443 : url.getPort(), chain); return null; });
                    int disposition = decision.get(job.timeout, TimeUnit.MILLISECONDS);
                    if (job.cancelled || disposition == 2) throw new CertificateException("Server trust rejected by connection delegate");
                    if (disposition == 1) system.checkServerTrusted(chain, type);

                } catch (CertificateException rejected) { throw rejected; }
                catch (Exception failure) { throw new CertificateException("Server trust callback failed", failure); }
                finally { job.authentication = null; }
            }
        };
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(null, new TrustManager[]{bridge}, null);
        connection.setSSLSocketFactory(context.getSocketFactory());
    }

    private static String[] headers(HttpURLConnection connection) {
        List<String> fields = new ArrayList<>();
        for (Map.Entry<String, List<String>> entry : connection.getHeaderFields().entrySet()) {
            if (entry.getKey() == null || entry.getValue() == null) continue;
            StringBuilder value = new StringBuilder();
            for (String part : entry.getValue()) { if (value.length() != 0) value.append(", "); value.append(part); }
            fields.add(entry.getKey()); fields.add(value.toString());
        }
        return fields.toArray(new String[0]);
    }

    private void run(Job job) {
        Runnable expiration = null;
        try {
            if(job.settings==null)job.settings = settings(job.token);
            if (job.settings[2] > 0) {
                expiration=() -> expire(job);
                if(!main.postDelayed(expiration,job.settings[2]))throw new IOException("Android main Looper is unavailable");
            }
            if (!decision(job, () -> redirect(job.token, null, 0, null, null))) return;
            for (int redirects = 0; ; redirects++) {
                awaitRunning(job);
                String[] info = requestInfo(job.token);
                URL url = new URL(info[0]);
                if (!url.getProtocol().equals("http") && !url.getProtocol().equals("https"))
                    throw new TransportError(-1002, "Unsupported URL scheme: " + url.getProtocol());
                job.timeout = (int) Math.max(1, Math.min(Integer.MAX_VALUE, Long.parseLong(info[2])));
                long length = Long.parseLong(info[4]);  
                String origin = url.getProtocol() + "://" + url.getHost().toLowerCase(java.util.Locale.ROOT) + ":" + url.getPort();
                java.util.concurrent.Semaphore permits = hostLimits.computeIfAbsent(job.settings[0] + ":" + origin,
                    ignored -> new java.util.concurrent.Semaphore((int) Math.max(1, Math.min(1024, job.settings[1])), true));
                synchronized(job){if(job.cancelled)throw new Cancelled();job.permitWaiter=Thread.currentThread();}
                try { permits.acquire(); }
                catch(InterruptedException interrupted){awaitRunning(job);Thread.currentThread().interrupt();throw new IOException("Connection limit wait interrupted",interrupted);}
                finally { synchronized(job){job.permitWaiter=null;} }
                try {
                awaitRunning(job);
                HttpURLConnection connection = (HttpURLConnection) url.openConnection();
                job.connection = connection;
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(job.timeout);
                connection.setReadTimeout(job.timeout);
                connection.setRequestMethod(info[1]);
                connection.setUseCaches(info[3].equals("0"));
                for (int i = 6; i + 1 < info.length; i += 2) connection.setRequestProperty(info[i], info[i + 1]);
                CookieJar cookies = job.settings[3] == 0 || info[5].equals("0") || job.settings[4]==0 ? null : cookieStores.computeIfAbsent(job.settings[4],ignored->new CookieJar());
                if (cookies != null) for (Map.Entry<String, List<String>> field : cookies.manager.get(url.toURI(), connection.getRequestProperties()).entrySet())
                    if (connection.getRequestProperty(field.getKey()) == null) connection.setRequestProperty(field.getKey(), String.join("; ", field.getValue()));
                if (!job.synchronous && connection instanceof HttpsURLConnection) configureTLS(job, (HttpsURLConnection) connection);
                // DEVELOPMENT_BEGIN
                Log.i("I2AGuest", "HTTP " + info[1] + " " + url.getHost());
                // DEVELOPMENT_END
                    if (length != -2) {
                        connection.setDoOutput(true);
                        if (length >= 0) connection.setFixedLengthStreamingMode(length);
                        else connection.setChunkedStreamingMode(32768);
                        byte[] buffer = new byte[32768];
                        long total = 0;
                        try (OutputStream output = connection.getOutputStream()) {
                            while (true) {
                                awaitRunning(job);
                                int count = readBody(job.token, buffer);
                                if (count < 0) throw new TransportError(-1021, "Request body stream failed");
                                if (count == 0) break;
                                output.write(buffer, 0, count);
                                total += count;
                                final long written = total;
                                onMain(job, () -> { sentBody(job.token, count, written, length); return null; });
                            }
                        }
                    }
                    int code = connection.getResponseCode();
                    if (cookies != null) cookies.put(url.toURI(), connection.getHeaderFields(),(int)job.settings[5]);
                    String[] fields = headers(connection);
                    String location = connection.getHeaderField("Location");
                    if (location != null && (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)) {
                        if (redirects >= 20) throw new TransportError(-1007, "Too many HTTP redirects");
                        String target = new URL(url, location).toExternalForm();
                        if (!decision(job, () -> redirect(job.token, url.toExternalForm(), code, fields, target))) return;
                        continue;
                    }
                    long expected = connection.getContentLengthLong();
                    String contentType = connection.getContentType();
                    String mime = null, encoding = null;
                    if (contentType != null) {
                        String[] parts = contentType.split(";"); mime = parts[0].trim();
                        for (int i = 1; i < parts.length; i++) {
                            String part = parts[i].trim();
                            if (part.regionMatches(true, 0, "charset=", 0, 8)) encoding = part.substring(8).replace("\"", "").trim();
                        }
                    }
                    final String responseMime = mime, responseEncoding = encoding;
                    if (!decision(job, () -> response(job.token, url.toExternalForm(), code, fields, expected, responseMime, responseEncoding))) return;
                    InputStream incoming = code >= 400 ? connection.getErrorStream() : connection.getInputStream();
                    if (incoming != null) try (InputStream input = incoming) {
                        byte[] buffer = new byte[32768];
                        for (int count; (count = input.read(buffer)) >= 0; ) {
                            awaitRunning(job);
                            if (count == 0) continue;
                            final int amount = count;
                            onMain(job, () -> { data(job.token, buffer, amount); return null; });
                        }
                    }
                    // DEVELOPMENT_BEGIN
                    Log.i("I2AGuest", "HTTP " + url.getHost() + " status=" + code);
                    // DEVELOPMENT_END
                    onMain(job, () -> { complete(job.token, 0, null); return null; });
                    return;
                } finally { if (job.connection != null) job.connection.disconnect(); job.connection = null; permits.release(); }
            }
        } catch (Cancelled cancelled) {
             
        } catch (Exception failure) {
            if (!job.cancelled) {
                Throwable cause = failure;
                while (cause.getCause() != null && !(cause instanceof IllegalStateException)) cause = cause.getCause();
                if (cause instanceof IllegalStateException) {
                    if (job.synchronous) throw new IllegalStateException(cause);
                    failures.report(cause.toString());
                }
                else {
                    int code = job.resourceExpired ? -1001 : failure instanceof TransportError ? ((TransportError) failure).code :
                        failure instanceof SocketTimeoutException ? -1001 : failure instanceof UnknownHostException ? -1003 :
                        failure instanceof ConnectException ? -1004 : failure instanceof SSLException ? -1200 : -1005;
                    if (expiration != null) main.removeCallbacks(expiration);
                    job.resourceExpired = false;
                    try { onMain(job, () -> { complete(job.token, code, failure.toString()); return null; }); }
                    catch (Exception callbackFailure) {
                        if (job.synchronous) throw new IllegalStateException(callbackFailure);
                        failures.report(callbackFailure.toString());
                    }
                }
            }
        } finally { if (expiration != null) main.removeCallbacks(expiration); jobs.remove(job.token); }
    }
}
