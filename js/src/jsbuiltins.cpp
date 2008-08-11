/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
 *
 * The Initial Developer of the Original Code is
 *   Andreas Gal <gal@mozilla.com>
 *
 * Contributor(s):
 *   Brendan Eich <brendan@mozilla.org>
 *   Mike Shaver <shaver@mozilla.org>
 *   David Anderson <danderson@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "jsstddef.h"
#include <math.h>

#include "jsapi.h"
#include "jsarray.h"
#include "jsbool.h"
#include "jscntxt.h"
#include "jsgc.h"
#include "jsiter.h"
#include "jsmath.h"
#include "jsnum.h"
#include "jsscope.h"
#include "jsstr.h"
#include "jstracer.h"

#include "nanojit/avmplus.h"
#include "nanojit/nanojit.h"

using namespace avmplus;
using namespace nanojit;

jsdouble FASTCALL
js_dmod(jsdouble a, jsdouble b)
{
    if (b == 0.0) {
        jsdpun u;
        u.s.hi = JSDOUBLE_HI32_EXPMASK | JSDOUBLE_HI32_MANTMASK;
        u.s.lo = 0xffffffff;
        return u.d;
    }
    jsdouble r;
#ifdef XP_WIN
    /* Workaround MS fmod bug where 42 % (1/0) => NaN, not 42. */
    if (JSDOUBLE_IS_FINITE(a) && JSDOUBLE_IS_INFINITE(b))
        r = a;
    else
#endif
        r = fmod(a, b);
    return r;
}

/* The following boxing/unboxing primitives we can't emit inline because
   they either interact with the GC and depend on Spidermonkey's 32-bit
   integer representation. */

jsval FASTCALL
js_BoxDouble(JSContext* cx, jsdouble d)
{
    jsint i;
    if (JSDOUBLE_IS_INT(d, i))
        return INT_TO_JSVAL(i);
    if (!cx->doubleFreeList) /* we must be certain the GC won't kick in */
        return JSVAL_ERROR_COOKIE;
    jsval v; /* not rooted but ok here because we know GC won't run */
    if (!js_NewDoubleInRootedValue(cx, d, &v))
        return JSVAL_ERROR_COOKIE;
    return v;
}

jsval FASTCALL
js_BoxInt32(JSContext* cx, jsint i)
{
    if (JS_LIKELY(INT_FITS_IN_JSVAL(i)))
        return INT_TO_JSVAL(i);
    if (!cx->doubleFreeList) /* we must be certain the GC won't kick in */
        return JSVAL_ERROR_COOKIE;
    jsval v; /* not rooted but ok here because we know GC won't run */
    jsdouble d = (jsdouble)i;
    if (!js_NewDoubleInRootedValue(cx, d, &v))
        return JSVAL_ERROR_COOKIE;
    return v;
} 

jsdouble FASTCALL
js_UnboxDouble(jsval v)
{
    if (JS_LIKELY(JSVAL_IS_INT(v)))
        return (jsdouble)JSVAL_TO_INT(v);
    return *JSVAL_TO_DOUBLE(v);
}

jsint FASTCALL
js_UnboxInt32(jsval v)
{
    if (JS_LIKELY(JSVAL_IS_INT(v)))
        return JSVAL_TO_INT(v);
    return js_DoubleToECMAInt32(*JSVAL_TO_DOUBLE(v));
}

int32 FASTCALL
js_DoubleToInt32(jsdouble d)
{
    return js_DoubleToECMAInt32(d);
}

int32 FASTCALL
js_DoubleToUint32(jsdouble d)
{
    return js_DoubleToECMAUint32(d);
}

jsdouble FASTCALL
js_Math_sin(jsdouble d)
{
    return sin(d);
}

jsdouble FASTCALL
js_Math_cos(jsdouble d)
{
    return cos(d);
}

jsdouble FASTCALL
js_Math_floor(jsdouble d)
{
    return floor(d);
}

jsdouble FASTCALL
js_Math_pow(jsdouble d, jsdouble p)
{
#ifdef NOTYET
    /* XXX Need to get a NaN here without parameterizing on context all the time. */
    if (!JSDOUBLE_IS_FINITE(p) && (d == 1.0 || d == -1.0))
        return NaN;
#endif
    if (p == 0)
        return 1.0;
    return pow(d, p);
}

jsdouble FASTCALL
js_Math_sqrt(jsdouble d)
{
    return sqrt(d);
}

bool FASTCALL
js_Array_dense_setelem(JSContext* cx, JSObject* obj, jsint i, jsval v)
{
    JS_ASSERT(OBJ_IS_DENSE_ARRAY(cx, obj));

    jsuint length = ARRAY_DENSE_LENGTH(obj);
    if ((jsuint)i < length) {
        if (obj->dslots[i] == JSVAL_HOLE) {
            if (i >= obj->fslots[JSSLOT_ARRAY_LENGTH])
                obj->fslots[JSSLOT_ARRAY_LENGTH] = i + 1;
            obj->fslots[JSSLOT_ARRAY_COUNT]++;
        }
        obj->dslots[i] = v;
        return true;
    }
    return OBJ_SET_PROPERTY(cx, obj, INT_TO_JSID(i), &v) ? true : false;
}

JSString* FASTCALL
js_Array_p_join(JSContext* cx, JSObject* obj, JSString *str)
{
    jsval v;
    if (!js_array_join_sub(cx, obj, TO_STRING, str, &v))
        return NULL;
    JS_ASSERT(JSVAL_IS_STRING(v));
    return JSVAL_TO_STRING(v);
}

JSString* FASTCALL
js_String_p_substring(JSContext* cx, JSString* str, jsint begin, jsint end)
{
    JS_ASSERT(end >= begin);
    return js_NewDependentString(cx, str, (size_t)begin, (size_t)(end - begin));
}

JSString* FASTCALL
js_String_p_substring_1(JSContext* cx, JSString* str, jsint begin)
{
    jsint end = JSSTRING_LENGTH(str);
    JS_ASSERT(end >= begin);
    return js_NewDependentString(cx, str, (size_t)begin, (size_t)(end - begin));
}

JSString* FASTCALL
js_FastConcatStrings(JSContext* cx, JSString* left, JSString* right)
{
    return js_ConcatStrings(cx, left, right, GCF_DONT_BLOCK);
}

JSString* FASTCALL
js_String_getelem(JSContext* cx, JSString* str, jsint i)
{
    if ((size_t)i >= JSSTRING_LENGTH(str))
        return NULL;
    /* XXX check for string freelist space */
    return js_GetUnitString(cx, str, (size_t)i);
}

JSString* FASTCALL
js_String_fromCharCode(JSContext* cx, jsint i)
{
    jschar c = (jschar)i;
    /* XXX check for string freelist space */
    if (c < UNIT_STRING_LIMIT)
        return js_GetUnitStringForChar(cx, c);
    return js_NewStringCopyN(cx, &c, 1);
}

jsint FASTCALL
js_String_p_charCodeAt(JSString* str, jsint i)
{
    if (i < 0 || (jsint)JSSTRING_LENGTH(str) <= i)
        return -1;
    return JSSTRING_CHARS(str)[i];
}

jsdouble FASTCALL
js_Math_random(JSRuntime* rt)
{
    JS_LOCK_RUNTIME(rt);
    js_random_init(rt);
    jsdouble z = js_random_nextDouble(rt);
    JS_UNLOCK_RUNTIME(rt);
    return z;
}

JSString* FASTCALL
js_String_p_concat_1int(JSContext* cx, JSString* str, jsint i)
{
    // FIXME: should be able to use stack buffer and avoid istr...
    JSString* istr = js_NumberToString(cx, i);
    if (!istr)
        return NULL;
    return js_ConcatStrings(cx, str, istr, GCF_DONT_BLOCK);
}

jsdouble FASTCALL
js_StringToNumber(JSContext* cx, JSString* str)
{
    const jschar* bp;
    const jschar* end;
    const jschar* ep;
    jsdouble d;

    JSSTRING_CHARS_AND_END(str, bp, end);
    if ((!js_strtod(cx, bp, end, &ep, &d) ||
         js_SkipWhiteSpace(ep, end) != end) &&
        (!js_strtointeger(cx, bp, end, &ep, 0, &d) ||
         js_SkipWhiteSpace(ep, end) != end)) {
        return *cx->runtime->jsNaN;
    }
    return d;
}

jsint FASTCALL
js_StringToInt32(JSContext* cx, JSString* str)
{
    const jschar* bp;
    const jschar* end;
    const jschar* ep;
    jsdouble d;

    JSSTRING_CHARS_AND_END(str, bp, end);
    if (!js_strtod(cx, bp, end, &ep, &d) || js_SkipWhiteSpace(ep, end) != end)
        return 0;
    return (jsint)d;
}

jsval FASTCALL
js_Any_getelem(JSContext* cx, JSObject* obj, JSString* idstr)
{
    jsval v;
    if (!OBJ_GET_PROPERTY(cx, obj, ATOM_TO_JSID(STRING_TO_JSVAL(idstr)), &v))
        return JSVAL_ERROR_COOKIE;
    return v;
}

bool FASTCALL
js_Any_setelem(JSContext* cx, JSObject* obj, JSString* idstr, jsval v)
{
    return OBJ_SET_PROPERTY(cx, obj, ATOM_TO_JSID(STRING_TO_JSVAL(idstr)), &v);
}

JSObject* FASTCALL
js_ValueToEnumerator(JSContext* cx, jsval v)
{
    if (!js_ValueToIterator(cx, JSITER_ENUMERATE, &v))
        return NULL;
    return JSVAL_TO_OBJECT(v);
}

GuardRecord* FASTCALL
js_CallTree(InterpState* state, Fragment* f)
{
    /* current we can't deal with inner trees that have globals so report an error */
    JS_ASSERT(!((TreeInfo*)f->vmprivate)->globalSlots.length());
    union { NIns *code; GuardRecord* (FASTCALL *func)(InterpState*, Fragment*); } u;
    u.code = f->code();
    return u.func(state, NULL);
}

JS_STATIC_ASSERT(JSSLOT_PRIVATE == JSSLOT_ARRAY_LENGTH);
JS_STATIC_ASSERT(JSSLOT_ARRAY_LENGTH + 1 == JSSLOT_ARRAY_COUNT);

JSObject* FASTCALL
js_FastNewObject(JSContext* cx, JSObject* ctor)
{
    JS_ASSERT(HAS_FUNCTION_CLASS(ctor));
    JSFunction* fun = GET_FUNCTION_PRIVATE(cx, ctor);
    JSClass* clasp = FUN_INTERPRETED(fun) ? &js_ObjectClass : fun->u.n.clasp;

    JSObject* obj = (JSObject*) js_NewGCThing(cx, GCF_DONT_BLOCK | GCX_OBJECT, sizeof(JSObject));
    if (!obj)
        return NULL;

    JS_LOCK_OBJ(cx, ctor);
    JSScope *scope = OBJ_SCOPE(ctor);
    JS_ASSERT(scope->object == ctor);
    JSAtom* atom = cx->runtime->atomState.classPrototypeAtom;

    JSScopeProperty *sprop = SCOPE_GET_PROPERTY(scope, ATOM_TO_JSID(atom));
    JS_ASSERT(SPROP_HAS_VALID_SLOT(sprop, scope));
    jsval v = LOCKED_OBJ_GET_SLOT(ctor, sprop->slot);
    JS_UNLOCK_SCOPE(cx, scope);

    JS_ASSERT(!JSVAL_IS_PRIMITIVE(v));
    JSObject* proto = JSVAL_TO_OBJECT(v);

    obj->fslots[JSSLOT_PROTO] = OBJECT_TO_JSVAL(proto);
    obj->fslots[JSSLOT_PARENT] = ctor->fslots[JSSLOT_PARENT];
    obj->fslots[JSSLOT_CLASS] = PRIVATE_TO_JSVAL(clasp);

    unsigned i = JSSLOT_PRIVATE;
    if (clasp == &js_ArrayClass) {
        obj->fslots[JSSLOT_ARRAY_LENGTH] = 0;
        obj->fslots[JSSLOT_ARRAY_COUNT] = 0;
        i += 2;
    }
    for (; i != JS_INITIAL_NSLOTS; ++i)
        obj->fslots[i] = JSVAL_VOID;

    obj->map = js_HoldObjectMap(cx, proto->map);
    obj->dslots = NULL;
    return obj;
}

bool FASTCALL
js_AddProperty(JSContext* cx, JSObject* obj, JSScopeProperty* sprop)
{
    JS_ASSERT(OBJ_IS_NATIVE(obj));
    JS_ASSERT(SPROP_HAS_STUB_SETTER(sprop));

    JS_LOCK_OBJ(cx, obj);
    JSScope* scope = OBJ_SCOPE(obj);
    if (scope->object == obj) {
        JS_ASSERT(!SCOPE_HAS_PROPERTY(scope, sprop));
    } else {
        scope = js_GetMutableScope(cx, obj);
        if (!scope) {
            JS_UNLOCK_OBJ(cx, obj);
            return false;
        }
    }

    uint32 slot = sprop->slot;
    if (!scope->table && sprop->parent == scope->lastProp && slot == scope->map.freeslot) {
        if (slot < STOBJ_NSLOTS(obj) && !OBJ_GET_CLASS(cx, obj)->reserveSlots) {
            ++scope->map.freeslot;
        } else {
            if (!js_AllocSlot(cx, obj, &slot)) {
                JS_UNLOCK_SCOPE(cx, scope);
                return false;
            }

            if (slot != sprop->slot)
                goto slot_changed;
        }

        SCOPE_EXTEND_SHAPE(cx, scope, sprop);
        ++scope->entryCount;
        scope->lastProp = sprop;
        JS_UNLOCK_SCOPE(cx, scope);
        return true;
    }

    JSScopeProperty* sprop2 = js_AddScopeProperty(cx, scope, sprop->id,
                                                  sprop->getter, sprop->setter, SPROP_INVALID_SLOT,
                                                  sprop->attrs, sprop->flags, sprop->shortid);
    if (sprop2 == sprop) {
        JS_UNLOCK_SCOPE(cx, scope);
        return true;
    }
    slot = sprop2->slot;

  slot_changed:
    js_FreeSlot(cx, obj, slot);
    JS_UNLOCK_SCOPE(cx, scope);
    return false;
}

JSString* FASTCALL
js_TypeOfObject(JSContext* cx, JSObject* obj)
{
    JSType type = JS_TypeOfValue(cx, OBJECT_TO_JSVAL(obj));
    return ATOM_TO_STRING(cx->runtime->atomState.typeAtoms[type]);
}

JSString* FASTCALL
js_TypeOfBoolean(JSContext* cx, jsint unboxed)
{
    jsval boxed = BOOLEAN_TO_JSVAL(unboxed);
    JS_ASSERT(JSVAL_IS_VOID(boxed) || JSVAL_IS_BOOLEAN(boxed));
    JSType type = JS_TypeOfValue(cx, boxed);
    return ATOM_TO_STRING(cx->runtime->atomState.typeAtoms[type]);
}

#define LO ARGSIZE_LO
#define F  ARGSIZE_F
#define Q  ARGSIZE_Q

#ifdef DEBUG
#define NAME(op) ,#op
#else
#define NAME(op)
#endif

#define BUILTIN1(op, at0, atr, tr, t0, cse, fold) \
    { (intptr_t)&js_##op, (at0 << 2) | atr, cse, fold NAME(op) },
#define BUILTIN2(op, at0, at1, atr, tr, t0, t1, cse, fold) \
    { (intptr_t)&js_##op, (at0 << 4) | (at1 << 2) | atr, cse, fold NAME(op) },
#define BUILTIN3(op, at0, at1, at2, atr, tr, t0, t1, t2, cse, fold) \
    { (intptr_t)&js_##op, (at0 << 6) | (at1 << 4) | (at2 << 2) | atr, cse, fold NAME(op) },
#define BUILTIN4(op, at0, at1, at2, at3, atr, tr, t0, t1, t2, t3, cse, fold)    \
    { (intptr_t)&js_##op, (at0 << 8) | (at1 << 6) | (at2 << 4) | (at3 << 2) | atr, cse, fold NAME(op) },

struct CallInfo builtins[] = {
#include "builtins.tbl"
};
