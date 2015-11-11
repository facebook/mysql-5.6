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
 * Unit test cases for FbsonDocument through FbsonJsonParser
 *
 * @author Tian Xia <tianx@fb.com>
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <fbson/FbsonJsonParser.h>
#include <fbson/FbsonDocument.h>
#include <fbson/FbsonUtil.h>

TEST(FBSON_PARSER, basic) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;
  fbson::FbsonToJson tojson;

  // keywords (null, true, false) are case insensitive
  // white spaces are pruned.
  std::string str(
      "{\"kn1\" : null,\"kn2\":NULL, \"kn3\":Null, \n \
        \"kt1\":true, \t \"kt2\":TRUE, \"kt3\":truE,\
        \"kf1\":false,\"kf2\":FALSE,\"kf3\":fAlse    }");

  // calling parse method 1
  EXPECT_TRUE(parser.parse(str));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // null value
  pval = doc->find("kn1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kn2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kn3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_STREQ("null", tojson.json(pval));
  EXPECT_EQ(0, pval->size());

  // true value
  pval = doc->find("kt1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kt2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kt3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_STREQ("true", tojson.json(pval));
  EXPECT_EQ(0, pval->size());

  // false value
  pval = doc->find("kf1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isFalse());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kf1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isFalse());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());

  pval = doc->find("kf1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isFalse());
  EXPECT_EQ(1, pval->numPackedBytes());
  EXPECT_STREQ("false", tojson.json(pval));
  EXPECT_EQ(0, pval->size());

  // negative test cases

  str.assign("{\"k1\":nul}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_SCALAR_VALUE, parser.getErrorCode());

  str.assign("{\"k1\":tru}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_SCALAR_VALUE, parser.getErrorCode());

  str.assign("{\"k1\":f}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_SCALAR_VALUE, parser.getErrorCode());

  str.assign("{\"k1\":");
  for (int i = 0; i < fbson::MaxNestingLevel; ++i)
    str += "{\"k1\":";
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_NESTING_LVL_OVERFLOW, parser.getErrorCode());
}

TEST(FBSON_PARSER, number_decimal) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;
  std::string str(
      "{\"k8\":123,\"k16\":-12345,\"k32\":1234567,\
        \"k64\":-1234567890123456789,\"kdbl1\":123.4567,\
        \"kdbl2\":1.234E308,\"kdbl3\":1.234e-307,\
        \"kdbl4\":1.234567890123456789}");

  // calling parse method 3
  EXPECT_TRUE(parser.parse(str.c_str(), (unsigned)str.size()));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // int8 value
  pval = doc->find("k8");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(123, ((fbson::Int8Val*)pval)->val());
  EXPECT_EQ(1, pval->size());
  EXPECT_EQ(123, *(int8_t*)(pval->getValuePtr()));

  // int16 value
  pval = doc->find("k16");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(3, pval->numPackedBytes());
  EXPECT_EQ(-12345, ((fbson::Int16Val*)pval)->val());
  EXPECT_EQ(2, pval->size());
  EXPECT_EQ(-12345, *(int16_t*)(pval->getValuePtr()));

  // int32 value
  pval = doc->find("k32");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ(1234567, ((fbson::Int32Val*)pval)->val());
  EXPECT_EQ(4, pval->size());
  EXPECT_EQ(1234567, *(int32_t*)(pval->getValuePtr()));

  // int64 value
  pval = doc->find("k64");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(-1234567890123456789, ((fbson::Int64Val*)pval)->val());
  EXPECT_EQ(8, pval->size());
  EXPECT_EQ(-1234567890123456789, *(int64_t*)(pval->getValuePtr()));

  // double value case 1
  pval = doc->find("kdbl1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_DOUBLE_EQ(123.4567, ((fbson::DoubleVal*)pval)->val());
  EXPECT_EQ(8, pval->size());
  EXPECT_DOUBLE_EQ(123.4567, *(double*)(pval->getValuePtr()));

  // double value case 2
  pval = doc->find("kdbl2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_DOUBLE_EQ(1.234e308, ((fbson::DoubleVal*)pval)->val());
  EXPECT_EQ(8, pval->size());
  EXPECT_DOUBLE_EQ(1.234e308, *(double*)(pval->getValuePtr()));

  // double value case 3
  pval = doc->find("kdbl3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_DOUBLE_EQ(1.234e-307, ((fbson::DoubleVal*)pval)->val());
  EXPECT_EQ(8, pval->size());
  EXPECT_DOUBLE_EQ(1.234e-307, *(double*)(pval->getValuePtr()));

  // double value case 4
  pval = doc->find("kdbl4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isDouble());
  EXPECT_EQ(9, pval->numPackedBytes());
  // this double value will be saved with 15 significant digits
  EXPECT_DOUBLE_EQ(1.2345678901234567, ((fbson::DoubleVal*)pval)->val());
  EXPECT_EQ(8, pval->size());
  EXPECT_DOUBLE_EQ(1.2345678901234567, *(double*)(pval->getValuePtr()));

  // negative test cases

  // invalid number
  str.assign("{\"k1\":}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":-}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":+}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":.}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.56E}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.56E-}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.56E+}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.56E+123.}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":123.56E+123.45}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());

  // invalid number
  str.assign("{\"k1\":12r45}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // decimal overflow
  str.assign("{\"k1\":123456789012345678901234567890}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DECIMAL_OVERFLOW, parser.getErrorCode());

  str.assign("{\"k1\":-123456789012345678901234567890}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DECIMAL_OVERFLOW, parser.getErrorCode());

  // double overflow
  str.assign("{\"k1\":9.999e308}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DOUBLE_OVERFLOW, parser.getErrorCode());

  str.assign("{\"k1\":-9.999e308}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DOUBLE_OVERFLOW, parser.getErrorCode());

  // exponent overflow
  str.assign("{\"k1\":1.2e309}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DOUBLE_OVERFLOW, parser.getErrorCode());

  str.assign("{\"k1\":1.2e-309}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_DOUBLE_OVERFLOW, parser.getErrorCode());

  // invalid exponent
  str.assign("{\"k1\":1.2e1a}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_EXPONENT, parser.getErrorCode());
}

TEST(FBSON_PARSER, number_hex) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;

  std::string str(
      "{\"khex0\":0x0,\"khex1\":0xAB,\"khex2\":0xABCD,\
        \"khex3\":0xABCDEFAB,\"khex4\":0xABCDEFABCDEFABCD,\
        \"khex5\":0x000A,\"khex6\":0x0000000A,\
        \"khex7\":0x000000000000000A}");

  // calling parse method 4
  fbson::FbsonInBuffer sb(str.c_str(), (unsigned)str.size());
  std::istream is(&sb);
  EXPECT_TRUE(parser.parse(is));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // hex value - int8
  pval = doc->find("khex0");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ((int8_t)0x0, ((fbson::Int8Val*)pval)->val());

  // hex value - int8
  pval = doc->find("khex1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ((int8_t)0xAB, ((fbson::Int8Val*)pval)->val());

  // hex value - int16
  pval = doc->find("khex2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(3, pval->numPackedBytes());
  EXPECT_EQ((int16_t)0xABCD, ((fbson::Int16Val*)pval)->val());

  // hex value - int32
  pval = doc->find("khex3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ((int32_t)0xABCDEFAB, ((fbson::Int32Val*)pval)->val());

  // hex value - int64
  pval = doc->find("khex4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ((int64_t)0xABCDEFABCDEFABCD, ((fbson::Int64Val*)pval)->val());

  // hex value - forcing int16
  pval = doc->find("khex5");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(3, pval->numPackedBytes());
  EXPECT_EQ((int16_t)0xA, ((fbson::Int16Val*)pval)->val());

  // hex value - forcing int32
  pval = doc->find("khex6");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ((int32_t)0xA, ((fbson::Int32Val*)pval)->val());

  // hex value - forcing int64
  pval = doc->find("khex7");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ((int64_t)0xA, ((fbson::Int64Val*)pval)->val());

  // negative cases

  // invalid hex
  str.assign("{\"k1\":0xABCH}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_HEX, parser.getErrorCode());

  // invalid hex
  str.assign("{\"k1\":0x}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_HEX, parser.getErrorCode());

  // invalid hex
  str.assign("{\"k1\":0x.}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_HEX, parser.getErrorCode());

  // sign before hex - makes it being interpreted as decimal
  str.assign("{\"k1\":-0xAB}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DECIMAL, parser.getErrorCode());

  // hex too long
  str.assign("{\"k1\":0x12345678901234567890}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_HEX_OVERFLOW, parser.getErrorCode());
}

TEST(FBSON_PARSER, number_octal) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;

  std::string str(
      "{\"koct1\":0123,\"koct2\":012345,\
        \"koct3\":01234567012,\
        \"koct4\":012345670123456701234}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // octal value - int8
  pval = doc->find("koct1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ((int8_t)0123, ((fbson::Int8Val*)pval)->val());

  // octal value - int16
  pval = doc->find("koct2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(3, pval->numPackedBytes());
  EXPECT_EQ((int16_t)012345, ((fbson::Int16Val*)pval)->val());

  // octal value - int32
  pval = doc->find("koct3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ((int32_t)01234567012, ((fbson::Int32Val*)pval)->val());

  // octal value - int64
  pval = doc->find("koct4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ((int64_t)012345670123456701234, ((fbson::Int64Val*)pval)->val());

  // negative cases

  // invalid octal
  str.assign("{\"k1\":019}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_OCTAL, parser.getErrorCode());

  // octal overflow
  str.assign("{\"k1\":012345670123456701234567}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_OCTAL_OVERFLOW, parser.getErrorCode());
}

TEST(FBSON_PARSER, string) {
  // use small outstream size, so it will grow automatically
  fbson::FbsonOutStream os(1);
  fbson::FbsonJsonParser parser(os);
  fbson::FbsonValue* pval;
  const char *raw_json;
  fbson::FbsonToJson tojson;

  std::string str(
    "{\"k1\":\"this is a test!\",\
                    \"k2\":\"\",\
                    \"k3\":\"this is an escaped quote \\\"!\",\
                    \"k4\":\"this is an escape char \\t!\",\
                    \"k5\":\"this is a new line \\n!\",\
                    \"k6\\tk6\":\"\\b\\f\\n\\r\\t\",\
                    \"k7\\u52a0\":\"\\u52a0\\u5dde\",\
                    \"k8\":\"\\u0002\",\
                    \"k9\":\"\\uD834\\uDD1E\"}");
  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      os.getBuffer(), (unsigned)os.getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // string value
  pval = doc->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
// packed bytes size: 1+4+strlen("this is a test!")
  EXPECT_EQ(20, pval->numPackedBytes());
  EXPECT_EQ(strlen("this is a test!"), pval->size());
  EXPECT_EQ("this is a test!", std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"this is a test!\""), strlen(raw_json));
  EXPECT_EQ("\"this is a test!\"", std::string(raw_json));

  // empty string
  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4
  EXPECT_EQ(5, pval->numPackedBytes());
  EXPECT_EQ(0, pval->size());
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"\""), strlen(raw_json));
  EXPECT_EQ("\"\"", std::string(raw_json));

  // string with escape characters
  pval = doc->find("k3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("this is an escaped quote \"!"), pval->size());
  EXPECT_EQ("this is an escaped quote \"!",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"this is an escaped quote \\\"!\""), strlen(raw_json));
  EXPECT_EQ("\"this is an escaped quote \\\"!\"", std::string(raw_json));

  // string with escape characters
  pval = doc->find("k4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("this is an escape char \t!"), pval->size());
  EXPECT_EQ("this is an escape char \t!",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"this is an escape char \\t!\""), strlen(raw_json));
  EXPECT_EQ("\"this is an escape char \\t!\"", std::string(raw_json));

  // string with escape characters
  pval = doc->find("k5");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("this is a new line \n!"), pval->size());
  EXPECT_EQ("this is a new line \n!",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"this is a new line \\n!\""), strlen(raw_json));
  EXPECT_EQ("\"this is a new line \\n!\"", std::string(raw_json));

  // string with escape characters in key and value
  pval = doc->find("k6\tk6");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("\b\f\n\r\t"), pval->size());
  EXPECT_EQ("\b\f\n\r\t",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"\\b\\f\\n\\r\\t\""), strlen(raw_json));
  EXPECT_EQ("\"\\b\\f\\n\\r\\t\"", std::string(raw_json));

  // string with escape Unicode characters
  pval = doc->find("k7\u52A0");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("\u52A0\u5DDE"), pval->size());
  EXPECT_EQ("\u52A0\u5DDE",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"\u52A0\u5DDE\""), strlen(raw_json));
  EXPECT_EQ("\"\u52A0\u5DDE\"", std::string(raw_json));

  // string with control characters
  pval = doc->find("k8");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("\u0002"), pval->size());
  EXPECT_EQ("\u0002",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"\\u0002\""), strlen(raw_json));
  EXPECT_EQ("\"\\u0002\"", std::string(raw_json));

  // string with UTF16
  pval = doc->find("k9");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("\U0001D11E"), pval->size());
  EXPECT_EQ("\U0001D11E",
            std::string(pval->getValuePtr(), pval->size()));
  raw_json = tojson.json(pval);
  EXPECT_EQ(strlen("\"\U0001D11E\""), strlen(raw_json));
  EXPECT_EQ("\"\U0001D11E\"", std::string(raw_json));

  str.assign(
      "{\"k\" :   \"1\", \
        \"kk\":   \"2\", \
        \"kkk\":  \"3\", \
        \"kkkk\": \"4\"}");
  EXPECT_TRUE(parser.parse(str.c_str()));
  pdoc = fbson::FbsonDocument::createDocument(os.getBuffer(),
                                              (unsigned)os.getSize());
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  pval = doc->find("k");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ("1", std::string(pval->getValuePtr(), pval->size()));

  pval = doc->find("kk");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ("2", std::string(pval->getValuePtr(), pval->size()));

  pval = doc->find("kkk");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ("3", std::string(pval->getValuePtr(), pval->size()));

  pval = doc->find("kkkk");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ("4", std::string(pval->getValuePtr(), pval->size()));

  // negative cases

  // invalid string (missing closing double-quote)
  str.assign("{\"k1\":\"incomplete}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_STR, parser.getErrorCode());

  // invalid UTF-8
  str.assign("{\"k1\":\"\\uD800\"}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_STR, parser.getErrorCode());

  // invalid UTF-16
  str.assign("{\"k1\":\"\\uD800\\uABCD\"}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_STR, parser.getErrorCode());

  // invalid escape: \a is not a valid escape char
  str.assign("{\"k1\":\"\\a\"}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_STR, parser.getErrorCode());
}

TEST(FBSON_PARSER, object) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;
  fbson::FbsonToJson tojson;

  std::string str(
      "{\"k1\":\"v1\",\"k2\":{\"k2_1\":\"v2_1\"},\
        \"k3\":[{\"k3_1\":\"v3_1\"}]}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  // object value
  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  // packed bytes size: 1+4+(1+strlen("k2_1"))+(1+4+strlen("v2_1"))
  EXPECT_EQ(19, pval->numPackedBytes());
  EXPECT_STREQ("{\"k2_1\":\"v2_1\"}", tojson.json(pval));

  // query into object value (level 2)
  pval = ((fbson::ObjectVal*)pval)->find("k2_1");
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
  EXPECT_EQ(19, pval->size());
  EXPECT_STREQ("[{\"k3_1\":\"v3_1\"}]", tojson.json(pval));

  // query into array (level 2)
  pval = ((fbson::ArrayVal*)pval)->get(0);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  // packed bytes size: 1+4+(1+strlen("k3_1"))+(1+4+strlen("v3_1"))
  EXPECT_EQ(19, pval->numPackedBytes());
  EXPECT_EQ(14, pval->size());
  EXPECT_STREQ("{\"k3_1\":\"v3_1\"}", tojson.json(pval));

  // further query into object value (level 3)
  pval = ((fbson::ObjectVal*)pval)->find("k3_1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v3_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v3_1"), pval->size());
  EXPECT_EQ("v3_1", std::string(pval->getValuePtr(), pval->size()));

  // find a value by key path
  pval = doc.getValue()->findPath("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v1")
  EXPECT_EQ(7, pval->numPackedBytes());
  EXPECT_EQ(strlen("v1"), pval->size());
  EXPECT_EQ("v1", std::string(pval->getValuePtr(), pval->size()));

  pval = doc.getValue()->findPath("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  // packed bytes size: 1+4+(1+strlen("k2_1"))+(1+4+strlen("v2_1"))
  EXPECT_EQ(19, pval->numPackedBytes());
  EXPECT_STREQ("{\"k2_1\":\"v2_1\"}", tojson.json(pval));

  pval = doc.getValue()->findPath("k2.k2_1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v2_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v2_1"), pval->size());
  EXPECT_EQ("v2_1", std::string(pval->getValuePtr(), pval->size()));

  // find a value by key path with string length
  pval = doc.getValue()->findPath("k3.0.k3_1", 9);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v3_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v3_1"), pval->size());
  EXPECT_EQ("v3_1", std::string(pval->getValuePtr(), pval->size()));

  // find a value by key path with custom delimiter
  pval = doc.getValue()->findPath("k3\t0\tk3_1", "\t");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v3_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v3_1"), pval->size());
  EXPECT_EQ("v3_1", std::string(pval->getValuePtr(), pval->size()));

  // find a value by key path with NULL delimiter
  pval = doc.getValue()->findPath("k2\0k2_1", 7, "" /* NULL delim */);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v2_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v2_1"), pval->size());
  EXPECT_EQ("v2_1", std::string(pval->getValuePtr(), pval->size()));

  // find a value by key path with NULL delimiter
  pval = doc.getValue()->findPath("k3\0000\0k3_1", 9, "" /* NULL delim */);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v3_1")
  EXPECT_EQ(9, pval->numPackedBytes());
  EXPECT_EQ(strlen("v3_1"), pval->size());
  EXPECT_EQ("v3_1", std::string(pval->getValuePtr(), pval->size()));

  // path doesn't exist
  pval = doc.getValue()->findPath("k4");
  EXPECT_TRUE(pval == nullptr);

  // path doesn't exist
  pval = doc.getValue()->findPath("k2.k2_2");
  EXPECT_TRUE(pval == nullptr);

  // empty key in the path
  pval = doc.getValue()->findPath("k3..k3_1");
  EXPECT_TRUE(pval == nullptr);

  // empty key in the path (trailing delimiter)
  pval = doc.getValue()->findPath("k3.0.");
  EXPECT_TRUE(pval == nullptr);

  // incorrect delimiter
  pval = doc.getValue()->findPath("k3\t0\tk3_1", "\\");
  EXPECT_TRUE(pval == nullptr);

  // array index is not a number
  pval = doc.getValue()->findPath("k3.0a.k3_1");
  EXPECT_TRUE(pval == nullptr);

  // array index out of range
  pval = doc.getValue()->findPath("k3.123456789.k3_1");
  EXPECT_TRUE(pval == nullptr);

  // empty object
  str.assign("{}");
  EXPECT_TRUE(parser.parse(str.c_str()));
  pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  // packed bytes size: 1(type)+4(size)
  EXPECT_EQ(5, doc->numPackedBytes());
  // no elements
  pval = doc->find("k1");
  EXPECT_TRUE(pval == nullptr);

  // negative cases

  // invalid document
  str.assign("");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_EMPTY_DOCUMENT, parser.getErrorCode());

  // invalid object (missing closing '}')
  str.assign("{\"k1\":1");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_OBJ, parser.getErrorCode());

  // invalid document (missing opening '}')
  str.assign("\"k1\":1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DOCU, parser.getErrorCode());

  // invalid key
  str.assign("{k1\":1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_OBJ, parser.getErrorCode());

  // invalid key
  str.assign("{\"k1:1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_KEY_STRING, parser.getErrorCode());

  // invalid key
  str.assign("{:1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_OBJ, parser.getErrorCode());

  // invalid key
  str.assign("{\"\":1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_KEY_STRING, parser.getErrorCode());

  // invalid key (over 64 bytes)
  str.assign("{\"This is a really long key string that exceeds"
             "64 bytes in total length\":1}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_KEY_LENGTH, parser.getErrorCode());

  // trailing garbage
  str.assign("{}}");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DOCU, parser.getErrorCode());
}

TEST(FBSON_PARSER, array) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;
  fbson::ArrayVal* parr;
  std::ostringstream ss;

  std::string str(
      "{\"array\":[null, true, false, 1, 300, -40000, 3000000000,\
                   \"string\", {\"k1\":\"v1\"}, [9,9,8,8]]}");

  EXPECT_TRUE(parser.parse(str.c_str()));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  pval = doc->find("array");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  parr = (fbson::ArrayVal*)pval;

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
  EXPECT_EQ(1, ((fbson::Int8Val*)pval)->val());

  pval = parr->get(4);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt16());
  EXPECT_EQ(300, ((fbson::Int16Val*)pval)->val());

  pval = parr->get(5);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt32());
  EXPECT_EQ(-40000, ((fbson::Int32Val*)pval)->val());

  pval = parr->get(6);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt64());
  EXPECT_EQ(3000000000, ((fbson::Int64Val*)pval)->val());

  pval = parr->get(7);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("string"), pval->size());
  EXPECT_EQ("string", std::string(pval->getValuePtr(), pval->size()));

  pval = parr->get(8);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isObject());
  pval = ((fbson::ObjectVal*)pval)->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  EXPECT_EQ(strlen("v1"), pval->size());
  EXPECT_EQ("v1", std::string(pval->getValuePtr(), pval->size()));

  pval = parr->get(9);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  EXPECT_EQ(4, ((fbson::ArrayVal*)pval)->numElem());

  // fail, negative index
  pval = parr->get(-1);
  EXPECT_TRUE(pval == nullptr);

  // empty array
  str.assign("{\"array\":[]}");
  EXPECT_TRUE(parser.parse(str.c_str()));
  pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  pval = doc->find("array");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isArray());
  // packed bytes size: 1(type)+4(size)
  EXPECT_EQ(5, pval->numPackedBytes());
  // no elements
  pval = parr->get(0);
  EXPECT_TRUE(pval == nullptr);

  // negative cases

  // invalid array (missing closing ']')
  str.assign("[1");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_ARR, parser.getErrorCode());

  // invalid document (missing opening ']')
  str.assign("1]");
  EXPECT_FALSE(parser.parse(str.c_str()));
  EXPECT_EQ(fbson::FbsonErrType::E_INVALID_DOCU, parser.getErrorCode());
}

// convert FBSON to JSON
TEST(FBSON_PARSER, Fbson_to_Json) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonValue* pval;
  fbson::FbsonToJson tojson;
  const char* json;

  std::string str(
      "{\"k0\" : \"This is a long long long long long long JSON text\",\
        \"k1\" : \"This is a long long long long long long JSON text\",\
        \"k2\" : \"This is a long long long long long long JSON text\",\
        \"k3\" : \"This is a long long long long long long JSON text\",\
        \"k4\" : \"This is a long long long long long long JSON text\",\
        \"k5\" : \"This is a long long long long long long JSON text\",\
        \"k6\" : \"This is a long long long long long long JSON text\",\
        \"k7\" : \"This is a long long long long long long JSON text\",\
        \"k8\" : \"This is a long long long long long long JSON text\",\
        \"k9\" : \"This is a long long long long long long JSON text\",\
        \"k10\" : \"This is a long long long long long long JSON text\",\
        \"k11\" : \"This is a long long long long long long JSON text\",\
        \"k12\" : \"This is a long long long long long long JSON text\",\
        \"k13\" : \"This is a long long long long long long JSON text\",\
        \"k14\" : \"This is a long long long long long long JSON text\",\
        \"k15\" : \"This is a long long long long long long JSON text\",\
        \"k16\" : \"This is a long long long long long long JSON text\",\
        \"k17\" : \"This is a long long long long long long JSON text\",\
        \"k18\" : \"This is a long long long long long long JSON text\",\
        \"k19\" : \"This is a long long long long long long JSON text\"}");

  EXPECT_TRUE(parser.parse(str));
  pval = fbson::FbsonDocument::createValue(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pval);

  json = tojson.json(pval);
  EXPECT_TRUE(json);
  EXPECT_EQ(strlen(json), 1151);

  // numbers
  str.assign(
      "{\"k1\": 2147483647,\
        \"k2\":-2147483648,\
        \"k3\": 9223372036854775807,\
        \"k4\":-9223372036854775808,\
        \"k5\":-1.234567890123456e+123}");

  EXPECT_TRUE(parser.parse(str));
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  pval = doc->find("k1");
  json = tojson.json(pval);
  EXPECT_TRUE(json);
  EXPECT_STREQ(json, "2147483647");

  pval = doc->find("k2");
  json = tojson.json(pval);
  EXPECT_TRUE(json);
  EXPECT_STREQ(json, "-2147483648");

  pval = doc->find("k3");
  json = tojson.json(pval);
  EXPECT_TRUE(json);
  EXPECT_STREQ(json, "9223372036854775807");

  pval = doc->find("k4");
  json = tojson.json(pval);
  EXPECT_TRUE(json);
  EXPECT_STREQ(json, "-9223372036854775808");

  pval = doc->find("k5");
  json = tojson.json(pval);
  EXPECT_TRUE(json);
  // should cap at 15 significant digits
  EXPECT_STREQ(json, "-1.23456789012346e+123");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
