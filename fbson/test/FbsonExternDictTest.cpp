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
#include <unordered_map>
#include <fbson/FbsonJsonParser.h>
#include <fbson/FbsonDocument.h>

// string-to-id dictionary
std::unordered_map<std::string, uint8_t> str_id_dict;
// id-to-string dictionary
std::vector<std::string> id_str_dict;
// maximum dictionary capacity
const int dict_max_size = 5;

// a simple dictionary insert function
int dictInsert(const char* key, unsigned len) {
  std::string kstr(key, len);
  auto iter = str_id_dict.find(kstr);
  if (iter != str_id_dict.end()) {
    return (int)iter->second;
  }

  if (id_str_dict.size() < dict_max_size) {
    id_str_dict.push_back(kstr);
    str_id_dict[kstr] = id_str_dict.size();
    return (int)id_str_dict.size();
  }

  return -1;
}

// a simple dictionary find function
int dictFind(const char* key, unsigned len) {
  std::string kstr(key, len);
  auto iter = str_id_dict.find(kstr);
  if (iter != str_id_dict.end()) {
    return (int)iter->second;
  }

  return -1;
}

TEST(FBSON_DICT, parser_dict) {
  fbson::FbsonJsonParser parser;
  fbson::FbsonDocument* pdoc;
  fbson::FbsonValue* pval;
  std::ostringstream ss;

  str_id_dict.clear();
  id_str_dict.clear();
  fbson::hDictInsert funcInsert = &dictInsert;
  fbson::hDictFind funcFind = &dictFind;

  std::string str(
      "{\"k1\":11,\"k2\":22,\"k3\":33,\
        \"k4\":44,\"k5\":55,\"k6\":66}");

  EXPECT_TRUE(parser.parse(str.c_str(), funcInsert));
  pdoc = fbson::FbsonDocument::createDocument(
      parser.getWriter().getOutput()->getBuffer(),
      (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_TRUE(pdoc);
  fbson::FbsonDocument& doc = *pdoc;
  EXPECT_EQ(dict_max_size, str_id_dict.size());
  EXPECT_EQ(dict_max_size, id_str_dict.size());

  // searching with key id directly
  pval = doc->find(1);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(11, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(2);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(22, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(3);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(33, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(4);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(44, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(5);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(55, ((fbson::Int8Val*)pval)->val());

  // fail since k6 is not mapped
  pval = doc->find(6);
  EXPECT_TRUE(pval == nullptr);

  // fail, negative index
  pval = doc->find(-1);
  EXPECT_TRUE(pval == nullptr);

  // searching with key string and dictionary
  pval = doc->find("k1", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(11, ((fbson::Int8Val*)pval)->val());

  // searching with mapped key string without dict will fail
  pval = doc->find("k1");
  EXPECT_TRUE(pval == nullptr);

  pval = doc->find("k2", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(22, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k3", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(33, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k4", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(44, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k5", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(55, ((fbson::Int8Val*)pval)->val());

  // searching with unmapped key string is ok
  pval = doc->find("k6", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(66, ((fbson::Int8Val*)pval)->val());

  // OK without dictionary
  pval = doc->find("k6");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(66, ((fbson::Int8Val*)pval)->val());
}

TEST(FBSON_DICT, writer_dict) {
  fbson::FbsonWriter writer;
  fbson::FbsonDocument* pdoc;
  fbson::FbsonValue* pval;
  std::ostringstream ss;

  str_id_dict.clear();
  id_str_dict.clear();
  fbson::hDictInsert funcInsert = &dictInsert;
  fbson::hDictFind funcFind = &dictFind;

  std::string str(
      "{\"k1\":11,\"k2\":22,\"k3\":33,\
        \"k4\":44,\"k5\":55,\"k6\":66}");

  EXPECT_TRUE(writer.writeStartObject());
  EXPECT_TRUE(writer.writeKey("k1", strlen("k1"), funcInsert));
  EXPECT_TRUE(writer.writeInt8(11));
  EXPECT_TRUE(writer.writeKey("k2", strlen("k2"), funcInsert));
  EXPECT_TRUE(writer.writeInt8(22));

  // calculate the key idx and insert directly
  int idx = funcInsert("k3", strlen("k3"));
  EXPECT_TRUE(idx > 0);
  EXPECT_TRUE(writer.writeKey(idx));
  EXPECT_TRUE(writer.writeInt8(33));

  EXPECT_TRUE(writer.writeKey("k4", strlen("k4"), funcInsert));
  EXPECT_TRUE(writer.writeInt8(44));
  EXPECT_TRUE(writer.writeKey("k5", strlen("k5"), funcInsert));
  EXPECT_TRUE(writer.writeInt8(55));
  EXPECT_TRUE(writer.writeKey("k6", strlen("k6"), funcInsert));
  EXPECT_TRUE(writer.writeInt8(66));

  EXPECT_TRUE(writer.writeEndObject());

  pdoc = fbson::FbsonDocument::createDocument(
      writer.getOutput()->getBuffer(), (unsigned)writer.getOutput()->getSize());
  fbson::FbsonDocument& doc = *pdoc;
  EXPECT_TRUE(pdoc);
  EXPECT_EQ(dict_max_size, str_id_dict.size());
  EXPECT_EQ(dict_max_size, id_str_dict.size());

  // searching with key id directly
  pval = doc->find(1);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(11, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(2);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(22, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(3);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(33, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(4);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(44, ((fbson::Int8Val*)pval)->val());

  pval = doc->find(5);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(55, ((fbson::Int8Val*)pval)->val());

  // fail since k6 is not mapped
  pval = doc->find(6);
  EXPECT_TRUE(pval == nullptr);

  // fail, negative index
  pval = doc->find(-1);
  EXPECT_TRUE(pval == nullptr);

  // searching with key string and dictionary
  pval = doc->find("k1", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(11, ((fbson::Int8Val*)pval)->val());

  // searching with mapped key string without dict will fail
  pval = doc->find("k1");
  EXPECT_TRUE(pval == nullptr);

  pval = doc->find("k2", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(22, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k3", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(33, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k4", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(44, ((fbson::Int8Val*)pval)->val());

  pval = doc->find("k5", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(55, ((fbson::Int8Val*)pval)->val());

  // searching with unmapped key string is ok
  pval = doc->find("k6", funcFind);
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(66, ((fbson::Int8Val*)pval)->val());

  // OK without dictionary
  pval = doc->find("k6");
  EXPECT_TRUE(pval != nullptr);
  EXPECT_TRUE(pval->isInt8());
  EXPECT_EQ(2, pval->numPackedBytes());
  EXPECT_EQ(66, ((fbson::Int8Val*)pval)->val());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
