/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class de_zib_jscip_nativ_jni_JniScipConsOr */

#ifndef _Included_de_zib_jscip_nativ_jni_JniScipConsOr
#define _Included_de_zib_jscip_nativ_jni_JniScipConsOr
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    includeConshdlrOr
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_includeConshdlrOr
  (JNIEnv *, jobject, jlong);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    createConsOr
 * Signature: (JLjava/lang/String;JI[JZZZZZZZZZZ)J
 */
JNIEXPORT jlong JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_createConsOr
  (JNIEnv *, jobject, jlong, jstring, jlong, jint, jlongArray, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    createConsBasicOr
 * Signature: (JLjava/lang/String;JI[J)J
 */
JNIEXPORT jlong JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_createConsBasicOr
  (JNIEnv *, jobject, jlong, jstring, jlong, jint, jlongArray);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    getNVarsOr
 * Signature: (JJ)I
 */
JNIEXPORT jint JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_getNVarsOr
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    getVarsOr
 * Signature: (JJ)[J
 */
JNIEXPORT jlongArray JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_getVarsOr
  (JNIEnv *, jobject, jlong, jlong);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsOr
 * Method:    getResultantOr
 * Signature: (JJ)J
 */
JNIEXPORT jlong JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsOr_getResultantOr
  (JNIEnv *, jobject, jlong, jlong);

#ifdef __cplusplus
}
#endif
#endif
