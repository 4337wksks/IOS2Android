package local.ios.host;

import android.app.KeyguardManager;
import android.content.Context;
import android.os.UserManager;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.AtomicFile;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class CredentialStore {
    private static final String ALIAS = BuildProfile.KEYCHAIN_ALIAS;
    private static final byte[] AAD = BuildProfile.KEYCHAIN_AAD.getBytes(StandardCharsets.UTF_8);
    private final AtomicFile file;
    private final KeyguardManager lock;
    private final UserManager user;
    CredentialStore(Context context) {
        file = new AtomicFile(new File(context.getNoBackupFilesDir(), "generic-passwords.aesgcm"));
        lock = (KeyguardManager) context.getSystemService(Context.KEYGUARD_SERVICE);
        user = context.getSystemService(UserManager.class);
    }
    private SecretKey key(boolean create) throws GeneralSecurityException, IOException {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(ALIAS)) return (SecretKey) store.getKey(ALIAS, null);
        if (!create) throw new GeneralSecurityException("Keychain encryption key is unavailable");
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setKeySize(256).build());
        return generator.generateKey();
    }
    synchronized byte[] read() throws GeneralSecurityException, IOException {
        if (!file.getBaseFile().exists()) return new byte[0];
        byte[] encoded = file.readFully();
        if (encoded.length < 32 || encoded[0] != 1) throw new IOException("Invalid keychain envelope");
        int size = encoded[1] & 255;
        if (size < 12 || size > 32 || size + 18 > encoded.length) throw new IOException("Invalid keychain nonce");
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, key(false), new GCMParameterSpec(128, encoded, 2, size));
        cipher.updateAAD(AAD);
        return cipher.doFinal(encoded, 2 + size, encoded.length - 2 - size);
    }
    synchronized void write(byte[] data) throws GeneralSecurityException, IOException {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, key(true));
        cipher.updateAAD(AAD);
        byte[] nonce = cipher.getIV(), encrypted = cipher.doFinal(data);
        byte[] encoded = ByteBuffer.allocate(2 + nonce.length + encrypted.length).put((byte) 1).put((byte) nonce.length).put(nonce).put(encrypted).array();
        FileOutputStream output = file.startWrite();
        try { output.write(encoded); file.finishWrite(output); }
        catch (IOException error) { file.failWrite(output); throw error; }
    }
    boolean unlocked() { return lock == null || !lock.isDeviceLocked(); }
    boolean afterFirstUnlock() { return user != null && user.isUserUnlocked(); }
}
