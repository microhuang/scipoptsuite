/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class de_zib_jscip_nativ_jni_JniScipConsDisjunction */

#ifndef _Included_de_zib_jscip_nativ_jni_JniScipConsDisjunction
#define _Included_de_zib_jscip_nativ_jni_JniScipConsDisjunction
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsDisjunction
 * Method:    includeConshdlrDisjunction
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsDisjunction_includeConshdlrDisjunction
  (JNIEnv *, jobject, jlong);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsDisjunction
 * Method:    createConsDisjunction
 * Signature: (JLjava/lang/String;I[JJZZZZZZ)J
 */
JNIEXPORT jlong JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsDisjunction_createConsDisjunction
  (JNIEnv *, jobject, jlong, jstring, jint, jlongArray, jlong, jboolean, jboolean, jboolean, jboolean, jboolean, jboolean);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsDisjunction
 * Method:    createConsBasicDisjunction
 * Signature: (JLjava/lang/String;I[JJ)J
 */
JNIEXPORT jlong JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsDisjunction_createConsBasicDisjunction
  (JNIEnv *, jobject, jlong, jstring, jint, jlongArray, jlong);

/*
 * Class:     de_zib_jscip_nativ_jni_JniScipConsDisjunction
 * Method:    addConsElemDisjunction
 * Signature: (JJJ)V
 */
JNIEXPORT void JNICALL Java_de_zib_jscip_nativ_jni_JniScipConsDisjunction_addConsElemDisjunction
  (JNIEnv *, jobject, jlong, jlong, jlong);

#ifdef __cplusplus
}
#endif
#endif
