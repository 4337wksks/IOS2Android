package local.ios.host;

import java.io.ByteArrayOutputStream;
import java.math.BigInteger;
import java.security.GeneralSecurityException;
import java.security.Key;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.security.Signature;
import java.security.interfaces.RSAKey;
import java.security.interfaces.RSAPrivateCrtKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import javax.crypto.Cipher;

final class RsaKeys {
    private static byte[] concatenate(byte[]... parts) {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        for (byte[] part : parts) output.write(part, 0, part.length);
        return output.toByteArray();
    }
    private static byte[] der(int tag, byte[] value) {
        ByteArrayOutputStream output = new ByteArrayOutputStream(); output.write(tag);
        if (value.length < 128) output.write(value.length);
        else {
            int bytes = 4; while (bytes > 1 && (value.length >>> ((bytes - 1) * 8)) == 0) --bytes;
            output.write(0x80 | bytes); for (int i = bytes - 1; i >= 0; --i) output.write(value.length >>> (i * 8));
        }
        output.write(value, 0, value.length); return output.toByteArray();
    }
    private static byte[] sequence(byte[]... items) { return der(0x30, concatenate(items)); }
    private static byte[] integer(BigInteger value) { return der(2, value.toByteArray()); }
    static byte[][] generatePair(int bits) throws GeneralSecurityException {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA"); generator.initialize(bits);
        KeyPair pair = generator.generateKeyPair();
        RSAPublicKey pub = (RSAPublicKey) pair.getPublic(); RSAPrivateCrtKey priv = (RSAPrivateCrtKey) pair.getPrivate();
        return new byte[][] {
            sequence(integer(pub.getModulus()), integer(pub.getPublicExponent())),
            sequence(integer(BigInteger.ZERO), integer(priv.getModulus()), integer(priv.getPublicExponent()), integer(priv.getPrivateExponent()),
                integer(priv.getPrimeP()), integer(priv.getPrimeQ()), integer(priv.getPrimeExponentP()), integer(priv.getPrimeExponentQ()), integer(priv.getCrtCoefficient()))
        };
    }
    static Key importKey(byte[] pkcs1, boolean privateKey) throws GeneralSecurityException {
        byte[] algorithm = new byte[] {0x30,0x0d,0x06,0x09,0x2a,(byte)0x86,0x48,(byte)0x86,(byte)0xf7,0x0d,0x01,0x01,0x01,0x05,0x00};
        KeyFactory factory = KeyFactory.getInstance("RSA");
        return privateKey ? factory.generatePrivate(new PKCS8EncodedKeySpec(sequence(integer(BigInteger.ZERO), algorithm, der(4, pkcs1)))) :
            factory.generatePublic(new X509EncodedKeySpec(sequence(algorithm, der(3, concatenate(new byte[] {0}, pkcs1)))));
    }
    static int bitLength(Key key) { return ((RSAKey) key).getModulus().bitLength(); }
    private static byte[] digestInfo(int padding, byte[] input) throws GeneralSecurityException {
        if (padding == 0 || padding == 1) return input;
        String hex; int length;
        switch (padding) {
            case 0x8001: hex = "3020300c06082a864886f70d020505000410"; length = 16; break;
            case 0x8002: hex = "3021300906052b0e03021a05000414"; length = 20; break;
            case 0x8003: hex = "302d300d06096086480165030402040500041c"; length = 28; break;
            case 0x8004: hex = "3031300d060960864801650304020105000420"; length = 32; break;
            case 0x8005: hex = "3041300d060960864801650304020205000430"; length = 48; break;
            case 0x8006: hex = "3051300d060960864801650304020305000440"; length = 64; break;
            default: throw new GeneralSecurityException("Unsupported SecKey signature padding");
        }
        if (input.length != length) throw new GeneralSecurityException("Digest length does not match SecKey padding");
        byte[] prefix = new byte[hex.length() / 2];
        for (int i = 0; i < prefix.length; i++) prefix[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
        return concatenate(prefix, input);
    }
    static byte[] sign(Key key, int padding, byte[] input) throws GeneralSecurityException {
        if (!(key instanceof java.security.PrivateKey)) throw new GeneralSecurityException("Signing requires a private key");
        if (padding == 0) {
            Cipher cipher = Cipher.getInstance("RSA/ECB/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE, key); return cipher.doFinal(input);
        }
        Signature signer = Signature.getInstance("NONEwithRSA"); signer.initSign((java.security.PrivateKey) key);
        signer.update(digestInfo(padding, input)); return signer.sign();
    }
    static boolean verify(Key key, int padding, byte[] input, byte[] signed) throws GeneralSecurityException {
        if (!(key instanceof java.security.PublicKey)) throw new GeneralSecurityException("Verification requires a public key");
        if (padding == 0) {
            Cipher cipher = Cipher.getInstance("RSA/ECB/NoPadding"); cipher.init(Cipher.DECRYPT_MODE, key);
            byte[] actual = cipher.doFinal(signed), expected = new byte[actual.length];
            if (input.length > expected.length) return false;
            System.arraycopy(input, 0, expected, expected.length - input.length, input.length);
            return MessageDigest.isEqual(actual, expected);
        }
        Signature verifier = Signature.getInstance("NONEwithRSA"); verifier.initVerify((java.security.PublicKey) key);
        verifier.update(digestInfo(padding, input)); return verifier.verify(signed);
    }
}
