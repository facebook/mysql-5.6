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

namespace fbson {

/*
 * FbsonToJson converts an FbsonValue object to a JSON string.
 */
class FbsonToJson {
 public:
  // get json string
  std::string json(const FbsonValue *val) {
    oss.str("");
    intern_json(val);
    return oss.str();
  }

 private:
  // recursively print value to oss
  void intern_json(const FbsonValue *val) {
    switch (val->type()) {
    case FbsonType::T_Null: {
      oss << "null";
      break;
    }
    case FbsonType::T_True: {
      oss << "true";
      break;
    }
    case FbsonType::T_False: {
      oss << "false";
      break;
    }
    case FbsonType::T_Int8: {
      oss << (int)(((Int8Val *)val)->val());
      break;
    }
    case FbsonType::T_Int16: {
      oss << (int)(((Int16Val *)val)->val());
      break;
    }
    case FbsonType::T_Int32: {
      oss << (int)(((Int32Val *)val)->val());
      break;
    }
    case FbsonType::T_Int64: {
      oss << (int64_t)(((Int64Val *)val)->val());
      break;
    }
    case FbsonType::T_Double: {
      oss << (double)(((DoubleVal *)val)->val());
      break;
    }
    case FbsonType::T_String: {
      oss << "\"";
      oss.write(((StringVal *)val)->getBlob(),
                ((StringVal *)val)->getBlobLen());
      oss << "\"";
      break;
    }
    case FbsonType::T_Binary: {
      oss << "\"<BINARY>";
      oss.write(((BinaryVal *)val)->getBlob(),
                ((BinaryVal *)val)->getBlobLen());
      oss << "<BINARY>\"";
      break;
    }
    case FbsonType::T_Object: {
      object_to_json((ObjectVal *)val);
      break;
    }
    case FbsonType::T_Array: {
      array_to_json((ArrayVal *)val);
      break;
    }
    default:
      break;
    }
  }

  // print object to oss
  void object_to_json(const ObjectVal *val) {
    oss << "{";

    auto iter = val->begin();
    auto iter_fence = val->end();

    while (iter < iter_fence) {
      // print key
      if (iter->klen()) {
        oss << "\"";
        oss.write(iter->getKeyStr(), iter->klen());
        oss << "\"";
      } else {
        oss << (int)iter->getKeyId();
      }
      oss << ":";

      // print value
      intern_json(iter->value());

      ++iter;
      if (iter != iter_fence) {
        oss << ",";
      }
    }

    assert(iter == iter_fence);

    oss << "}";
  }

  // print array to json
  void array_to_json(const ArrayVal *val) {
    oss << "[";

    auto iter = val->begin();
    auto iter_fence = val->end();

    while (iter != iter_fence) {
      // print value
      intern_json((const FbsonValue *)iter);
      ++iter;
      if (iter != iter_fence) {
        oss << ",";
      }
    }

    assert(iter == iter_fence);

    oss << "]";
  }

 private:
  std::ostringstream oss;
};

} // namespace fbson

#endif // FBSON_FBSONUTIL_H
