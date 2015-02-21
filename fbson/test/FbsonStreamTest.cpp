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
#include <fbson/FbsonJsonParser.h>
#include <fbson/FbsonDocument.h>

// parser using std::istream and std::ostream as input and output streams
TEST(FBSON_STREAM, parser_std_stream) {
  std::stringbuf sb;
  std::ostream os(&sb);
  fbson::FbsonJsonParserT<std::ostream> parser(os);
  fbson::FbsonValue* pval;

  std::string str("{\"k1\":\"v1\",\"k2\":123,\"k3\":null,\"k4\":true}");

  // calling parse method 3 using std::istringstream
  std::istringstream iss(str);
  EXPECT_TRUE(parser.parse(iss));
  EXPECT_TRUE(sb.in_avail() > 0);

  unsigned len = (unsigned)sb.in_avail();
  char buf[len];
  sb.sgetn(buf, len);
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(buf, len);
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  pval = doc->find("k1");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isString());
  // packed bytes size: 1+4+strlen("v1")
  EXPECT_EQ(7, pval->numPackedBytes());
  EXPECT_EQ(strlen("v1"), ((fbson::StringVal*)pval)->getBlobLen());
  EXPECT_EQ("v1",
            std::string(((fbson::StringVal*)pval)->getBlob(),
                        ((fbson::StringVal*)pval)->getBlobLen()));

  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(123, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k3");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isNull());
  EXPECT_EQ(1, pval->numPackedBytes());

  pval = doc->find("k4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());
}

// writer using std::istream and std::ostream as input and output streams
TEST(FBSON_STREAM, writer_std_stream) {
  std::stringbuf sb;
  std::ostream os(&sb);
  fbson::FbsonWriterT<std::ostream> writer(os);
  fbson::FbsonValue* pval;

  EXPECT_TRUE(writer.writeStartObject());

  EXPECT_TRUE(writer.writeKey("k1", strlen("k1")));
  EXPECT_TRUE(writer.writeStartString());
  EXPECT_TRUE(writer.writeString("v1", strlen("v1")));
  EXPECT_TRUE(writer.writeEndString());
  EXPECT_TRUE(writer.writeKey("k2", strlen("k2")));
  EXPECT_TRUE(writer.writeInt8(123));
  EXPECT_TRUE(writer.writeKey("k3", strlen("k3")));
  EXPECT_TRUE(writer.writeNull());
  EXPECT_TRUE(writer.writeKey("k4", strlen("k4")));
  EXPECT_TRUE(writer.writeBool(true));

  EXPECT_TRUE(writer.writeEndObject());
  EXPECT_TRUE(sb.in_avail() > 0);

  unsigned len = (unsigned)sb.in_avail();
  char buf[len];
  sb.sgetn(buf, len);
  fbson::FbsonDocument* pdoc = fbson::FbsonDocument::createDocument(buf, len);
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;

  pval = doc->find("k2");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(123, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k4");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isTrue());
  EXPECT_EQ(1, pval->numPackedBytes());

  // reset buffer
  sb.str(std::string());
  writer.reset();

  // empty object
  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeEndObject());
  EXPECT_EQ(6, sb.in_avail());

  len = (unsigned)sb.in_avail();
  sb.sgetn(buf, len);
  pdoc = fbson::FbsonDocument::createDocument(buf, len);
  EXPECT_TRUE(pdoc);
  doc = *pdoc;

  // packed bytes size: 1(type)+4(size)
  EXPECT_EQ(5, doc->numPackedBytes());
  // no elements
  pval = doc->find("k1");
  EXPECT_TRUE(pval == nullptr);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
