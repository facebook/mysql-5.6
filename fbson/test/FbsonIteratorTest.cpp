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
 * Unit test cases for FBSON iterators
 *
 * @author Tian Xia <tianx@fb.com>
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <fbson/FbsonDocument.h>
#include <fbson/FbsonJsonParser.h>
#include <fbson/FbsonUtil.h>

TEST(FBSON_ITERATOR, basic_obj) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonToJson tojson;

  std::string str("{\"k1\":\"v1\",\"k2\":null,\"k3\":true,\"k4\":2}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  auto iter = doc->begin();
  auto iter_fence = doc->end();
  EXPECT_TRUE(iter != iter_fence);

  // first key-value pair
  EXPECT_TRUE(iter->value()->isString());
  // size: (1+strlen("k1"))+(1+4+strlen("v1"))
  EXPECT_EQ(10, iter->numPackedBytes());
  EXPECT_EQ("k1", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("\"v1\"", tojson.json(iter->value()));

  ++iter;

  // second key-value pair
  EXPECT_TRUE(iter->value()->isNull());
  // size: (1+strlen("k2"))+1
  EXPECT_EQ(4, iter->numPackedBytes());
  EXPECT_EQ("k2", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("null", tojson.json(iter->value()));

  ++iter;

  // third key-value pair
  EXPECT_TRUE(iter->value()->isTrue());
  // size: (1+strlen("k2"))+1
  EXPECT_EQ(4, iter->numPackedBytes());
  EXPECT_EQ("k3", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("true", tojson.json(iter->value()));

  ++iter;

  // fourth key-value pair
  EXPECT_TRUE(iter->value()->isInt8());
  // size: (1+strlen("k2"))+2
  EXPECT_EQ(5, iter->numPackedBytes());
  EXPECT_EQ("k4", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("2", tojson.json(iter->value()));

  ++iter;

  EXPECT_EQ(iter, iter_fence);

  // constant iterator
  fbson::ObjectVal::const_iterator const_iter = doc->begin();
  fbson::ObjectVal::const_iterator const_iter_fence = doc->end();
  EXPECT_TRUE(const_iter != const_iter_fence);

  // skip first
  ++const_iter;

  // skip second
  ++const_iter;

  // third key-value pair
  EXPECT_TRUE(const_iter->value()->isTrue());
  // size: (1+strlen("k2"))+1
  EXPECT_EQ(4, const_iter->numPackedBytes());
  EXPECT_EQ("k3", std::string(const_iter->getKeyStr(), const_iter->klen()));
  EXPECT_STREQ("true", tojson.json(const_iter->value()));

  ++const_iter;

  // skip fourth
  ++const_iter;

  EXPECT_EQ(const_iter, const_iter_fence);
}

TEST(FBSON_ITERATOR, basic_arr) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue *pval;
  fbson::ArrayVal *parr;

  std::string str("{\"array\":[\"v1\",null,true,2]}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  pval = doc->find("array");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());

  parr = (fbson::ArrayVal *)pval;
  auto iter = parr->begin();
  auto iter_fence = parr->end();
  EXPECT_TRUE(iter != iter_fence);

  // first key-value pair
  EXPECT_TRUE(iter->isString());
  // size: (1+4+strlen("v1"))
  EXPECT_EQ(7, iter->numPackedBytes());
  auto str_val = (fbson::StringVal *)((fbson::FbsonValue *)iter);
  EXPECT_EQ("v1", std::string(str_val->getBlob(), str_val->getBlobLen()));

  ++iter;

  // second key-value pair
  EXPECT_TRUE(iter->isNull());
  EXPECT_EQ(1, iter->numPackedBytes());

  ++iter;

  // third key-value pair
  EXPECT_TRUE(iter->isTrue());
  EXPECT_EQ(1, iter->numPackedBytes());

  ++iter;

  // fourth key-value pair
  EXPECT_TRUE(iter->isInt8());
  EXPECT_EQ(2, iter->numPackedBytes());
  EXPECT_EQ(2, ((fbson::Int8Val *)((fbson::FbsonValue *)iter))->val());

  ++iter;

  EXPECT_EQ(iter, iter_fence);

  // constant iterator
  fbson::ArrayVal::const_iterator const_iter = parr->begin();
  fbson::ArrayVal::const_iterator const_iter_fence = parr->end();
  EXPECT_TRUE(const_iter != const_iter_fence);

  // skip first
  ++const_iter;

  // skip second
  ++const_iter;

  // third key-value pair
  EXPECT_TRUE(const_iter->isTrue());
  EXPECT_EQ(1, const_iter->numPackedBytes());

  ++const_iter;

  // skip fourth
  ++const_iter;

  EXPECT_EQ(const_iter, const_iter_fence);
}

TEST(FBSON_ITERATOR, nested_iter) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue *pval;
  fbson::FbsonToJson tojson;

  std::string str(
      "{\"k1\":\"v1\",\"k2\":{\"k2_1\":\"v2_1\"},\
        \"k3\":[1,2]}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  auto iter = doc->begin();
  auto iter_fence = doc->end();
  EXPECT_TRUE(iter != iter_fence);

  // first key-value pair
  EXPECT_TRUE(iter->value()->isString());
  // size: (1+strlen("k1"))+(1+4+strlen("v1"))
  EXPECT_EQ(10, iter->numPackedBytes());
  EXPECT_EQ("k1", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("\"v1\"", tojson.json(iter->value()));

  ++iter;

  // second key-value pair
  EXPECT_TRUE(iter->value()->isObject());
  // size: (1+strlen("k2"))+1+4+(1+strlen("k2_1"))+(1+4+strlen("v2_1"))
  EXPECT_EQ(22, iter->numPackedBytes());
  EXPECT_EQ("k2", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("{\"k2_1\":\"v2_1\"}", tojson.json(iter->value()));

  // query into second pair's object value (level 2)
  fbson::ObjectVal *pobj = (fbson::ObjectVal *)(iter->value());
  auto iter2 = pobj->begin();
  auto iter2_fence = pobj->end();

  EXPECT_TRUE(iter2->value()->isString());
  // size: (1+strlen("k2_1"))+1+4+strlen("v2_1")
  EXPECT_EQ(14, iter2->numPackedBytes());
  EXPECT_EQ("k2_1", std::string(iter2->getKeyStr(), iter2->klen()));
  EXPECT_STREQ("\"v2_1\"", tojson.json(iter2->value()));
  ++iter2;
  EXPECT_EQ(iter2, iter2_fence);

  ++iter;

  // back to third key-value pair (level 1)
  EXPECT_TRUE(iter->value()->isArray());
  // size: (1+strlen("k3"))+1+4+2+2
  EXPECT_EQ(12, iter->numPackedBytes());
  EXPECT_EQ("k3", std::string(iter->getKeyStr(), iter->klen()));
  EXPECT_STREQ("[1,2]", tojson.json(iter->value()));

  // query into third pair's array value (level 2)
  auto arr = (fbson::ArrayVal *)(iter->value());
  auto iter3 = arr->begin();
  auto iter3_fence = arr->end();

  EXPECT_TRUE(iter3->isInt8());
  EXPECT_EQ(2, iter3->numPackedBytes());
  EXPECT_EQ(1, ((fbson::Int8Val *)((fbson::FbsonValue *)iter3))->val());
  ++iter3;

  EXPECT_TRUE(iter3->isInt8());
  EXPECT_EQ(2, iter3->numPackedBytes());
  EXPECT_EQ(2, ((fbson::Int8Val *)((fbson::FbsonValue *)iter3))->val());
  ++iter3;
  EXPECT_EQ(iter3, iter3_fence);

  ++iter;

  EXPECT_EQ(iter, iter_fence);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
