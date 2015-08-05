/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/*
 * This header file defines miscellaneous utility classes.
 *
 * @author Tian Xia <tianx@fb.com>
 */

#ifndef FBSON_FBSONUTIL_H
#define FBSON_FBSONUTIL_H

#include <sstream>
#include "FbsonDocument.h"
#include "FbsonStream.h"
namespace fbson {

#define OUT_BUF_SIZE 1024

/*
 * FbsonToJson converts an FbsonValue object to a JSON string.
 */
class FbsonToJson {
 public:
  FbsonToJson() : os_(buffer_, OUT_BUF_SIZE) {}

  // get json string
  const char* json(const FbsonValue* pval) {
    os_.clear();
    os_.seekp(0);

    if (pval) {
      intern_json(pval);
    }

    os_.put(0);
    return os_.getBuffer();
  }

 private:
  // recursively convert FbsonValue
  void intern_json(const FbsonValue* val) {
    switch (val->type()) {
    case FbsonType::T_Null: {
      os_.write("null", 4);
      break;
    }
    case FbsonType::T_True: {
      os_.write("true", 4);
      break;
    }
    case FbsonType::T_False: {
      os_.write("false", 5);
      break;
    }
    case FbsonType::T_Int8: {
      os_.write(((Int8Val*)val)->val());
      break;
    }
    case FbsonType::T_Int16: {
      os_.write(((Int16Val*)val)->val());
      break;
    }
    case FbsonType::T_Int32: {
      os_.write(((Int32Val*)val)->val());
      break;
    }
    case FbsonType::T_Int64: {
      os_.write(((Int64Val*)val)->val());
      break;
    }
    case FbsonType::T_Double: {
      os_.write(((DoubleVal*)val)->val());
      break;
    }
    case FbsonType::T_String: {
      string_to_json(((StringVal*)val)->getBlob(),
                     ((StringVal*)val)->length());
      break;
    }
    case FbsonType::T_Binary: {
      os_.write("\"<BINARY>", 9);
      os_.write(((BinaryVal*)val)->getBlob(), ((BinaryVal*)val)->getBlobLen());
      os_.write("<BINARY>\"", 9);
      break;
    }
    case FbsonType::T_Object: {
      object_to_json((ObjectVal*)val);
      break;
    }
    case FbsonType::T_Array: {
      array_to_json((ArrayVal*)val);
      break;
    }
    default:
      break;
    }
  }

  void string_to_json(const char *str, size_t len){
    os_.put('"');
    if(nullptr == str){
      os_.put('"');
      return;
    }
    char char_buffer[16];
    for(const char *ptr = str; ptr != str + len && *ptr; ++ptr){
      if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\')
        os_.put(*ptr);
      else {
        os_.put('\\');
        unsigned char token;
        switch (token = *ptr) {
          case '\\':
            os_.put('\\');
            break;
          case '\"':
            os_.put('\"');
            break;
          case '\b':
            os_.put('b');
            break;
          case '\f':
            os_.put('f');
            break;
          case '\n':
            os_.put('n');
            break;
          case '\r':
            os_.put('r');
            break;
          case '\t':
            os_.put('t');
            break;
          default: {
            int char_num = snprintf(char_buffer,
                                    sizeof(char_buffer),
                                    "u%04x",
                                    token);
            os_.write(char_buffer, char_num);
            break;
          }
        }
      }
    }
    os_.put('"');
  }

  // convert object
  void object_to_json(const ObjectVal* val) {
    os_.put('{');

    auto iter = val->begin();
    auto iter_fence = val->end();

    while (iter < iter_fence) {
      // write key
      if (iter->klen()) {
        string_to_json(iter->getKeyStr(), iter->klen());
      } else {
        os_.write(iter->getKeyId());
      }
      os_.put(':');

      // convert value
      intern_json(iter->value());

      ++iter;
      if (iter != iter_fence) {
        os_.put(',');
      }
    }

    assert(iter == iter_fence);

    os_.put('}');
  }

  // convert array to json
  void array_to_json(const ArrayVal* val) {
    os_.put('[');

    auto iter = val->begin();
    auto iter_fence = val->end();

    while (iter != iter_fence) {
      // convert value
      intern_json((const FbsonValue*)iter);
      ++iter;
      if (iter != iter_fence) {
        os_.put(',');
      }
    }

    assert(iter == iter_fence);

    os_.put(']');
  }

 private:
  FbsonOutStream os_;
  char buffer_[OUT_BUF_SIZE];
};

// This class is a utility to create a FbsonValue.
template<class OS_TYPE>
class FbsonValueCreaterT {
 public:
  FbsonValue *operator()(int32_t val){
    return operator()((int64_t)val);
  }

  FbsonValue *operator()(int64_t val){
    writer_.reset();
    writer_.writeStartArray();
    writer_.writeInt(val);
    writer_.writeEndArray();
    return extractValue();
  }
  FbsonValue *operator()(double val){
    writer_.reset();
    writer_.writeStartArray();
    writer_.writeDouble(val);
    writer_.writeEndArray();
    return extractValue();
  }
  FbsonValue *operator()(const char* str){
    return operator()(str, (unsigned int)strlen(str));
  }
  FbsonValue *operator()(const char* str, unsigned int str_len){
    writer_.reset();
    writer_.writeStartArray();
    writer_.writeStartString();
    writer_.writeString(str, str_len);
    writer_.writeEndString();
    writer_.writeEndArray();
    return extractValue();
  }
  FbsonValue *operator()(bool val){
    writer_.reset();
    writer_.writeStartArray();
    writer_.writeBool(val);
    writer_.writeEndArray();
    return extractValue();
  }
  FbsonValue *operator()(){
    writer_.reset();
    writer_.writeStartArray();
    writer_.writeNull();
    writer_.writeEndArray();
    return extractValue();
  }

 private:
  FbsonValue *extractValue(){
    return static_cast<ArrayVal*>(
      FbsonDocument::createValue(writer_.getOutput()->getBuffer(),
                                 (int)writer_.getOutput()->getSize()))->get(0);
  }
  FbsonWriterT<OS_TYPE> writer_;
};
typedef FbsonValueCreaterT<FbsonOutStream> FbsonValueCreater;
} // namespace fbson

#endif // FBSON_FBSONUTIL_H
