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
 * Unit test cases for FbsonDocument through FbsonWriter
 *
 * @author Tian Xia <tianx@fb.com>
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <fbson/FbsonDocument.h>
#include <fbson/FbsonWriter.h>
#include <fbson/FbsonUtil.h>

TEST(FBSON_WRITER, basic) {
  fbson::FbsonWriter writer;
  fbson::FbsonValue *pval;

  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k1", strlen("k1")));
  EXPECT_TRUE(writer.writeNull());
  EXPECT_TRUE(writer.writeKey("k2", strlen("k2")));
  EXPECT_TRUE(writer.writeBool(true));
  EXPECT_TRUE(writer.writeKey("k3", strlen("k3")));
  EXPECT_TRUE(writer.writeBool(false));
  EXPECT_TRUE(writer.writeEndObject());

  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  // null value
  pval = doc->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());
  EXPECT_EQ(1, pval->numPackedBytes());

  // true value
  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());

  // false value
  pval = doc->find("k3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isFalse());
  EXPECT_EQ(1, pval->numPackedBytes());

  writer.reset();

  // empty object
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeEndObject());
  pdoc = fbson::FbsonDocument::createDocument(writer.getOutput()->getBuffer(),
                                              writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  // packed bytes size: 1(type)+4(size)
  EXPECT_EQ(5, doc->numPackedBytes());
  // no elements
  pval = doc->find("k1");
  EXPECT_TRUE(pval == nullptr);

  writer.reset();

  // empty array
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("array", strlen("array")));
  EXPECT_TRUE(writer.writeStartArray());
  EXPECT_TRUE(writer.writeEndArray());
  EXPECT_TRUE(writer.writeEndObject());

  pdoc = fbson::FbsonDocument::createDocument(writer.getOutput()->getBuffer(),
                                              writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  pval = doc->find("array");
  EXPECT_TRUE(pval->isArray());

  // packed bytes size: 1(type)+4(size)
  EXPECT_EQ(5, pval->numPackedBytes());
  // no elements
  pval = ((fbson::ArrayVal *)pval)->get(0);
  EXPECT_TRUE(pval == nullptr);

  // negative test cases

  // can't keep writing without resetting
  EXPECT_FALSE(writer.writeStartObject());
  EXPECT_FALSE(writer.writeEndObject());
  EXPECT_FALSE(writer.writeStartArray());
  EXPECT_FALSE(writer.writeEndArray());
  EXPECT_FALSE(writer.writeStartString());
  EXPECT_FALSE(writer.writeEndString());
  EXPECT_FALSE(writer.writeStartBinary());
  EXPECT_FALSE(writer.writeEndBinary());
  EXPECT_FALSE(writer.writeKey("k4", 2));
  EXPECT_FALSE(writer.writeNull());

  writer.reset();

  // can't start writing without starting an object or array first
  EXPECT_FALSE(writer.writeEndObject());
  EXPECT_FALSE(writer.writeKey("k1", 2));
  EXPECT_FALSE(writer.writeNull());
  EXPECT_FALSE(writer.writeStartString());
  EXPECT_FALSE(writer.writeEndString());
  EXPECT_FALSE(writer.writeStartBinary());
  EXPECT_FALSE(writer.writeEndBinary());

  // OK
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k1", 2));
  EXPECT_TRUE(writer.writeNull());
  // missing key
  EXPECT_FALSE(writer.writeNull());

  EXPECT_TRUE(writer.writeKey("k2", 2));
  // missing value
  EXPECT_FALSE(writer.writeKey("k3", 2));

  // incomplete object will not load
  pdoc = fbson::FbsonDocument::createDocument(writer.getOutput()->getBuffer(),
                                              writer.getOutput()->getSize());
  EXPECT_FALSE(pdoc);
}

TEST(FBSON_WRITER, number_decimal) {
  fbson::FbsonWriter writer;
  fbson::FbsonValue *pval;

  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k8", strlen("k8")));
  EXPECT_TRUE(writer.writeInt8(123));
  EXPECT_TRUE(writer.writeKey("k16", strlen("k16")));
  EXPECT_TRUE(writer.writeInt16(-12345));
  EXPECT_TRUE(writer.writeKey("k32", strlen("k32")));
  EXPECT_TRUE(writer.writeInt32(1234567));
  EXPECT_TRUE(writer.writeKey("k64", strlen("k64")));
  EXPECT_TRUE(writer.writeInt64(-1234567890123456789));
  EXPECT_TRUE(writer.writeKey("kdbl1", strlen("kdbl1")));
  EXPECT_TRUE(writer.writeDouble(123.4567));
  EXPECT_TRUE(writer.writeKey("kdbl2", strlen("kdbl2")));
  EXPECT_TRUE(writer.writeDouble(1.234E308));
  EXPECT_TRUE(writer.writeKey("kdbl3", strlen("kdbl3")));
  EXPECT_TRUE(writer.writeDouble(1.234e-308));
  EXPECT_TRUE(writer.writeEndObject());

  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  // int8 value
  pval = doc->find("k8");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(123, ((fbson::Int8Val *)pval)->val());

  // int16 value
  pval = doc->find("k16");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(3, pval->numPackedBytes());
  EXPECT_EQ(-12345, ((fbson::Int16Val *)pval)->val());

  // int32 value
  pval = doc->find("k32");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ(1234567, ((fbson::Int32Val *)pval)->val());

  // int64 value
  pval = doc->find("k64");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(-1234567890123456789, ((fbson::Int64Val *)pval)->val());

  // double value case 1
  pval = doc->find("kdbl1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_DOUBLE_EQ(123.4567, ((fbson::DoubleVal *)pval)->val());

  // double value case 2
  pval = doc->find("kdbl2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_DOUBLE_EQ(1.234e308, ((fbson::DoubleVal *)pval)->val());

  // double value case 3
  pval = doc->find("kdbl3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_NEAR(1.234e-308, ((fbson::DoubleVal *)pval)->val(), 0.0001e-308);
}

TEST(FBSON_WRITER, string) {
  // use small outstream size, so it will grow automatically
  fbson::FbsonOutStream os(1);
  fbson::FbsonWriter writer(os);
  fbson::FbsonValue *pval;
  std::string str("this is a test!");

  EXPECT_TRUE(writer.writeStartObject());

  // string
  EXPECT_TRUE(writer.writeKey("k1", strlen("k1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString(str.c_str(), str.size()));
  EXPECT_TRUE(writer.writeEndString());
  // empty string
  EXPECT_TRUE(writer.writeKey("k2", strlen("k2")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeEndString());

  // binary
  EXPECT_TRUE(writer.writeKey("k3", strlen("k3")));
  EXPECT_TRUE(writer.writeStartBinary());
  EXPECT_TRUE(writer.writeBinary(str.c_str(), str.size()));
  EXPECT_TRUE(writer.writeEndBinary());
  // empty binary
  EXPECT_TRUE(writer.writeKey("k4", strlen("k4")));
  EXPECT_TRUE(writer.writeStartBinary());
  EXPECT_TRUE(writer.writeEndBinary());

  EXPECT_TRUE(writer.writeEndObject());

  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  // string value
  pval = doc->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("this is a test!")
  EXPECT_EQ(20, pval->numPackedBytes());
  EXPECT_EQ(str.size(), pval->size());
  EXPECT_EQ(str, std::string(pval->getValuePtr(), pval->size()));

  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ(0, ((fbson::BlobVal *)pval)->getBlobLen());

  // binary
  pval = doc->find("k3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isBinary());
  // packed bytes size: 1+4+strlen("this is a test!")
  EXPECT_EQ(20, pval->numPackedBytes());
  EXPECT_EQ(str.size(), pval->size());
  EXPECT_EQ(str, std::string(pval->getValuePtr(), pval->size()));

  pval = doc->find("k4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isBinary());
  // packed bytes size: 1+4
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());
}

TEST(FBSON_WRITER, object) {
  fbson::FbsonWriter writer;
  fbson::FbsonValue *pval;
  fbson::FbsonToJson tojson;

  EXPECT_TRUE(writer.writeStartObject());

  EXPECT_TRUE(writer.writeKey("k1", strlen("k1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("v1", strlen("v1")));
  EXPECT_TRUE(writer.writeEndString());

  EXPECT_TRUE(writer.writeKey("k2", strlen("k2")));
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k2_1", strlen("k2_1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("v2_1", strlen("v2_1")));
  EXPECT_TRUE(writer.writeEndString());
  EXPECT_TRUE(writer.writeEndObject());

  EXPECT_TRUE(writer.writeKey("k3", strlen("k3")));
  EXPECT_TRUE(writer.writeStartArray());
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k3_1", strlen("k3_1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("v3_1", strlen("v3_1")));
  EXPECT_TRUE(writer.writeEndString());
  EXPECT_TRUE(writer.writeEndObject());
  EXPECT_TRUE(writer.writeEndArray());

  EXPECT_TRUE(writer.writeEndObject());

  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  // object value
  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  // packed bytes size: 1+4+(1+strlen("k2_1"))+(1+4+strlen("v2_1"))
  EXPECT_EQ(19, pval->numPackedBytes());
  EXPECT_STREQ("{\"k2_1\":\"v2_1\"}", tojson.json(pval));

  // query into object value (level 2)
  pval = ((fbson::ObjectVal *)pval)->find("k2_1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v2_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v2_1"), pval->size());
  EXPECT_EQ("v2_1", std::string(pval->getValuePtr(), pval->size()));

  // array value
  pval = doc->find("k3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  // packed bytes: 1+4+(19)
  EXPECT_EQ(24, pval->numPackedBytes());
  EXPECT_STREQ("[{\"k3_1\":\"v3_1\"}]", tojson.json(pval));

  // query into array (level 2)
  pval = ((fbson::ArrayVal *)pval)->get(0);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  // packed bytes size: 1+4+(1+strlen("k3_1"))+(1+4+strlen("v3_1"))
  EXPECT_EQ(19, pval->numPackedBytes());
  EXPECT_STREQ("{\"k3_1\":\"v3_1\"}", tojson.json(pval));

  // further query into object value (level 3)
  pval = ((fbson::ObjectVal *)pval)->find("k3_1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v3_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v3_1"), pval->size());
  EXPECT_EQ("v3_1", std::string(pval->getValuePtr(), pval->size()));
}

TEST(FBSON_WRITER, array) {
  fbson::FbsonWriter writer;
  fbson::FbsonValue *pval;
  fbson::ArrayVal *parr;

  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("array", strlen("array")));
  EXPECT_TRUE(writer.writeStartArray());

  EXPECT_TRUE(writer.writeNull());
  EXPECT_TRUE(writer.writeBool(true));
  EXPECT_TRUE(writer.writeBool(false));
  EXPECT_TRUE(writer.writeInt8(1));
  EXPECT_TRUE(writer.writeInt16(300));
  EXPECT_TRUE(writer.writeInt32(-40000));
  EXPECT_TRUE(writer.writeInt64(3000000000));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("string", strlen("string")));
  EXPECT_TRUE(writer.writeEndString());

  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k1", strlen("k1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("v1", strlen("v1")));
  EXPECT_TRUE(writer.writeEndString());

  EXPECT_TRUE(writer.writeEndObject());

  EXPECT_TRUE(writer.writeStartArray());
  EXPECT_TRUE(writer.writeInt8(9));
  EXPECT_TRUE(writer.writeInt8(9));
  EXPECT_TRUE(writer.writeInt8(8));
  EXPECT_TRUE(writer.writeInt8(8));
  EXPECT_TRUE(writer.writeEndArray());

  EXPECT_TRUE(writer.writeEndArray());
  EXPECT_TRUE(writer.writeEndObject());

  fbson::FbsonDocument *pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), writer.getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument &doc = *pdoc;

  pval = doc->find("array");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  parr = (fbson::ArrayVal *)pval;

  pval = parr->get(0);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());

  pval = parr->get(1);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());

  pval = parr->get(2);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isFalse());

  pval = parr->get(3);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(1, ((fbson::Int8Val *)pval)->val());

  pval = parr->get(4);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(300, ((fbson::Int16Val *)pval)->val());

  pval = parr->get(5);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(-40000, ((fbson::Int32Val *)pval)->val());

  pval = parr->get(6);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(3000000000, ((fbson::Int64Val *)pval)->val());

  pval = parr->get(7);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("string"), pval->size());
  EXPECT_EQ("string", std::string(pval->getValuePtr(), pval->size()));

  pval = parr->get(8);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  pval = ((fbson::ObjectVal *)pval)->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("v1"), pval->size());
  EXPECT_EQ("v1", std::string(pval->getValuePtr(), pval->size()));

  pval = parr->get(9);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  EXPECT_EQ(4, ((fbson::ArrayVal *)pval)->numElem());

  // fail, negative index
  pval = parr->get(-1);
  EXPECT_TRUE(pval == nullptr);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
