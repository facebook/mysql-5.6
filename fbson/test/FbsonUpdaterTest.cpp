#include <string>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <fbson/FbsonJsonParser.h>
#include <fbson/FbsonDocument.h>
#include <fbson/FbsonUpdater.h>
#include <fbson/FbsonUtil.h>
TEST(FBSON_UPDATER, document) {
  using namespace fbson;
  char buffer[1024];
  FbsonDocument *doc = nullptr;
  FbsonToJson to_json;

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Null);
  EXPECT_STREQ("null", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_True);
  EXPECT_STREQ("true", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_False);
  EXPECT_STREQ("false", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Int8);
  ((Int8Val*)doc->getValue())->setVal(10);
  EXPECT_STREQ("10", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Int16);
  ((Int16Val*)doc->getValue())->setVal(1000);
  EXPECT_STREQ("1000", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Int32);
  ((Int32Val*)doc->getValue())->setVal(1000000);
  EXPECT_STREQ("1000000", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Int64);
  ((Int64Val*)doc->getValue())->setVal(10000000000);
  EXPECT_STREQ("10000000000", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Double);
  ((DoubleVal*)doc->getValue())->setVal(123.123);
  EXPECT_STREQ("123.123", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_String);
  EXPECT_STREQ("\"\"", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Object);
  EXPECT_STREQ("{}", to_json.json(doc->getValue()));

  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_Array);
  EXPECT_STREQ("[]", to_json.json(doc->getValue()));

  FbsonValueCreater creater;
  doc = FbsonDocument::makeDocument(buffer,
                                    sizeof(buffer),
                                    FbsonType::T_String);
  doc->setValue(creater("ABCDEFG"));
  EXPECT_STREQ("\"ABCDEFG\"", to_json.json(doc->getValue()));

  ((StringVal*)doc->getValue())->setVal("A", strlen("A"));
  EXPECT_EQ(1, ((StringVal*)doc->getValue())->length());

  ((StringVal*)doc->getValue())->setVal("", 0);
  EXPECT_EQ(0, ((StringVal*)doc->getValue())->length());
}
TEST(FBSON_UPDATER, access) {
  std::string str("{\"str\": \"value1\","\
                  "\"dict\": {\"key1\": 123, \"key2\": \"AAADCCCDDFFEFER\"},"\
                  "\"arr\": [\"abc\", \"def\", 1234,"\
                  "{\"key1\":1, \"key2\": 2}],"      \
                 "\"num\":111}");
  using namespace fbson;
  FbsonJsonParser parser;
  FbsonToJson to_json;
  EXPECT_TRUE(parser.parse(str));
  const int buffer_size = 1024;
  char buffer[buffer_size];
  memcpy(buffer,
         parser.getWriter().getOutput()->getBuffer(),
         (unsigned)parser.getWriter().getOutput()->getSize());

  FbsonUpdater updater(FbsonDocument::createDocument(
                         buffer,
                         (unsigned)parser.getWriter().getOutput()->getSize()),
                       buffer_size);
  // Get the element in the dict
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("dict"));
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("key1"));
  EXPECT_STREQ("123", to_json.json(updater.getCurrent()));

  // Get the dict
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("dict"));
  EXPECT_STREQ("{\"key1\":123,\"key2\":\"AAADCCCDDFFEFER\"}",
               to_json.json(updater.getCurrent()));

  // Get the array
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("arr"));
  EXPECT_STREQ("[\"abc\",\"def\",1234,{\"key1\":1,\"key2\":2}]",
               to_json.json(updater.getCurrent()));

  // Get the elem in array
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("arr"));
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey(3));
  EXPECT_STREQ("{\"key1\":1,\"key2\":2}",
               to_json.json(updater.getCurrent()));

  // Get the string
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("str"));
  EXPECT_STREQ("\"value1\"",
               to_json.json(updater.getCurrent()));

  // Get the number
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num"));
  EXPECT_STREQ("111",
               to_json.json(updater.getCurrent()));

  // Negative test
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_KEYNOTEXIST, updater.pushPathKey("xxx"));
  EXPECT_EQ(FbsonErrType::E_NOTARRAY, updater.pushPathKey(123));

  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("arr"));
  EXPECT_EQ(FbsonErrType::E_OUTOFBOUNDARY, updater.pushPathKey(10));
  EXPECT_EQ(FbsonErrType::E_OUTOFBOUNDARY, updater.pushPathKey(-1));
  EXPECT_EQ(FbsonErrType::E_NOTOBJ, updater.pushPathKey("xxx"));
}

TEST(FBSON_UPDATER, remove) {
  std::string str("{\"str\": \"value1\","                               \
                  "\"num1\": 123,"                                      \
                  "\"num2\": 0x12,"                                     \
                  "\"num3\": 0x1234,"                                   \
                  "\"num4\": 0x12345678,"                               \
                  "\"num5\": 0x123456789ABCDEF0,"                       \
                  "\"dict\": {\"key1\": 123, \"key2\": \"AAADCCCDDFFEFER\"}," \
                  "\"arr\": [\"abc\", \"def\", 1234,"                   \
                  "{\"key1\":1, \"key2\": 2}]}");

  using namespace fbson;
  FbsonToJson to_json;
  FbsonJsonParser parser;
  EXPECT_TRUE(parser.parse(str));
  const int buffer_size = 1024;
  char buffer[buffer_size];
  memcpy(buffer,
         parser.getWriter().getOutput()->getBuffer(),
         (unsigned)parser.getWriter().getOutput()->getSize());
  FbsonUpdater updater(FbsonDocument::createDocument(
                         buffer,
                         (unsigned)parser.getWriter().getOutput()->getSize()),
                       buffer_size);

  // Test delete in array
  updater.pushPathKey("arr");
  updater.pushPathKey(3);
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","                                   \
               "\"num1\":123,"                                          \
               "\"num2\":18,"                                           \
               "\"num3\":4660,"                                         \
               "\"num4\":305419896,"                                    \
               "\"num5\":1311768467463790320,"                          \
               "\"dict\":{\"key1\":123,\"key2\":\"AAADCCCDDFFEFER\"},"  \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));


  // Test delete the element in the dict
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("dict"));
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("key2"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","            \
               "\"num1\":123,"                   \
               "\"num2\":18,"                    \
               "\"num3\":4660,"                  \
               "\"num4\":305419896,"             \
               "\"num5\":1311768467463790320,"   \
               "\"dict\":{\"key1\":123},"        \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  // Test delete the dict
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("dict"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","            \
               "\"num1\":123,"                   \
               "\"num2\":18,"                    \
               "\"num3\":4660,"                  \
               "\"num4\":305419896,"             \
               "\"num5\":1311768467463790320,"   \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  // Test the delete of numbers
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num3"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","               \
               "\"num1\":123,"                      \
               "\"num2\":18,"                       \
               "\"num4\":305419896,"                \
               "\"num5\":1311768467463790320,"      \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num2"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","               \
               "\"num1\":123,"                      \
               "\"num4\":305419896,"                \
               "\"num5\":1311768467463790320,"      \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num1"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","               \
               "\"num4\":305419896,"                \
               "\"num5\":1311768467463790320,"      \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num4"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","           \
               "\"num5\":1311768467463790320,"  \
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("num5"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\","\
               "\"arr\":[\"abc\",\"def\",1234]}"
               , to_json.json(updater.getRoot()));

  // Test delete arr
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("arr"));
  updater.remove();
  EXPECT_STREQ("{\"str\":\"value1\"}", to_json.json(updater.getRoot()));

  // Test delete string
  updater.clearPath();
  EXPECT_EQ(FbsonErrType::E_NONE, updater.pushPathKey("str"));
  updater.remove();
  EXPECT_STREQ("{}", to_json.json(updater.getRoot()));
}

TEST(FBSON_UPDATER, add_array) {
  std::string str("[1,2,3]");
  using namespace fbson;
  FbsonJsonParser parser;
  FbsonToJson to_json;
  EXPECT_TRUE(parser.parse(str));
  const int buffer_size = 128;
  char buffer[buffer_size];
  memcpy(buffer,
         parser.getWriter().getOutput()->getBuffer(),
         (unsigned)parser.getWriter().getOutput()->getSize());

  FbsonUpdater updater(FbsonDocument::createDocument(
                         buffer,
                         (unsigned)parser.getWriter().getOutput()->getSize()),
                       buffer_size);

  FbsonValue *to_be_added;
  FbsonWriter writer;
  // Add an int8
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt8(10);
  writer.writeEndArray();
  to_be_added = ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(0, to_be_added));
  EXPECT_STREQ("[10,1,2,3]", to_json.json(updater.getRoot()));

  // Add an int64 to the end of array
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt64(10234);
  writer.writeEndArray();
  to_be_added =
    ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.appendValue(to_be_added));
  EXPECT_STREQ("[10,1,2,3,10234]", to_json.json(updater.getRoot()));

  // Add an int16 to the middle of array
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt32(1111);
  writer.writeEndArray();
  to_be_added =
    ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(2, to_be_added));
  EXPECT_STREQ("[10,1,1111,2,3,10234]", to_json.json(updater.getRoot()));

  // Add an int32 to the middle of array
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt32(99);
  writer.writeEndArray();
  to_be_added =
    ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(4, to_be_added));
  EXPECT_STREQ("[10,1,1111,2,99,3,10234]", to_json.json(updater.getRoot()));

  // Add an string to the middle of array
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeStartString();
  writer.writeString(str);
  writer.writeEndString();
  writer.writeEndArray();
  to_be_added =
    ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(4, to_be_added));
  EXPECT_STREQ("[10,1,1111,2,\"[1,2,3]\",99,3,10234]",
               to_json.json(updater.getRoot()));

  // Add an array to the middle of array
  updater.clearPath();
  to_be_added = FbsonDocument::createValue(
    parser.getWriter().getOutput()->getBuffer(),
    (unsigned)parser.getWriter().getOutput()->getSize());
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(4, to_be_added));
  EXPECT_STREQ("[10,1,1111,2,[1,2,3],\"[1,2,3]\",99,3,10234]",
               to_json.json(updater.getRoot()));
  // Add an dic to the middle of array
  updater.clearPath();
  writer.reset();
  writer.writeStartObject();

  writer.writeKey("key1");
  writer.writeStartString();
  writer.writeString("AAA");
  writer.writeEndString();

  writer.writeKey("key2");
  writer.writeNull();

  writer.writeEndObject();
  to_be_added =
    (ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize());
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(4, to_be_added));
  EXPECT_STREQ(
    "[10,1,1111,2,{\"key1\":\"AAA\",\"key2\":null},\
[1,2,3],\"[1,2,3]\",99,3,10234]",
    to_json.json(updater.getRoot()));

  // Add to the index exceed the boundary
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt32(911);
  writer.writeEndArray();
  to_be_added = ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);

  EXPECT_EQ(FbsonErrType::E_OUTOFBOUNDARY,
            updater.insertValue(15, to_be_added));
  EXPECT_STREQ(
    "[10,1,1111,2,{\"key1\":\"AAA\",\"key2\":null},\
[1,2,3],\"[1,2,3]\",99,3,10234]",
    to_json.json(updater.getRoot()));

  // using insertAll to add all elements in an array

  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeInt32(911);
  writer.writeInt32(123);
  writer.writeBool(false);
  writer.writeEndArray();
  ArrayVal *arr_be_added = (ArrayVal*)FbsonDocument::createValue(
    writer.getOutput()->getBuffer(),
    (int)writer.getOutput()->getSize());
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(2, arr_be_added->begin(),
                                arr_be_added->end()));
  EXPECT_STREQ("[10,1,911,123,false,1111,2,"\
               "{\"key1\":\"AAA\",\"key2\":null},[1,2,3],\"[1,2,3]\","  \
               "99,3,10234]",
    to_json.json(updater.getRoot()));

  // Add string to make buffer full
  updater.clearPath();
  writer.reset();
  writer.writeStartArray();
  writer.writeStartString();

  std::string large_str("AAAAAAAA"); // 8 bytes
  large_str += large_str; // 16 bytes
  large_str += large_str; // 32 bytes
  large_str += large_str; // 64 bytes
  large_str += large_str; // 128 bytes
  int remain = buffer_size - updater.getDocument()->numPackedBytes() -
    sizeof(FbsonType) - sizeof(int);
  printf("REMAIN=%d\n", remain);
  large_str.resize(remain);
  writer.writeString(large_str);
  writer.writeEndString();
  writer.writeEndArray();
  to_be_added =
    ((ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()))->get(0);
  EXPECT_EQ(FbsonErrType::E_NONE,
            updater.insertValue(7, to_be_added));

  EXPECT_STREQ(
    (std::string("[10,1,911,123,false,1111,2,\"") +
     large_str +
     std::string("\",{\"key1\":\"AAA\",\"key2\":null},") +
     std::string("[1,2,3],\"[1,2,3]\",99,3,10234]")).c_str(),
    to_json.json(updater.getRoot()));

  EXPECT_EQ(updater.getDocument()->numPackedBytes(), buffer_size);

  // Add one more will lead to out of memory error
  writer.reset();
  writer.writeStartArray();
  writer.writeNull();
  writer.writeEndArray();
  to_be_added =
    (ArrayVal*)FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize());
  EXPECT_EQ(FbsonErrType::E_OUTOFMEMORY,
            updater.insertValue(7, to_be_added));

  // And nothing change
  EXPECT_STREQ(
    (std::string("[10,1,911,123,false,1111,2,\"") +
     large_str +
     std::string("\",{\"key1\":\"AAA\",\"key2\":null},") +
     std::string("[1,2,3],\"[1,2,3]\",99,3,10234]")).c_str(),
    to_json.json(updater.getRoot()));
}
TEST(FBSON_UPDATER, add_dictionary) {
  std::string str("{\"str\": \"1234\",\"key2\": 0xabcd}");
  using namespace fbson;
  FbsonJsonParser parser;
  FbsonToJson to_json;
  EXPECT_TRUE(parser.parse(str));

  const int buffer_size = 128;
  char buffer[buffer_size];
  memcpy(buffer,
         parser.getWriter().getOutput()->getBuffer(),
         (unsigned)parser.getWriter().getOutput()->getSize());

  FbsonUpdater updater(FbsonDocument::createDocument(
                         buffer,
                         (unsigned)parser.getWriter().getOutput()->getSize()),
                       buffer_size);
  FbsonWriter writer;
  writer.writeStartObject();
  writer.writeKey("ABC");
  writer.writeInt(123);
  writer.writeKey("STR");
  writer.writeStartArray();
  writer.writeStartString();
  writer.writeString("LLLLLL");
  writer.writeEndString();
  writer.writeEndArray();
  writer.writeEndObject();
  ObjectVal *obj = static_cast<ObjectVal*>(writer.getValue());

  updater.insertValue(obj->begin(), obj->end());
  EXPECT_STREQ("{\"str\":\"1234\",\"key2\":-21555,"\
               "\"ABC\":123,\"STR\":[\"LLLLLL\"]}",
               to_json.json(updater.getRoot()));

}

TEST(FBSON_UPDATER, update) {
  std::string str("{\"str\": \"1234\",\"key2\": 0xabcd}");
  using namespace fbson;
  FbsonJsonParser parser;
  FbsonToJson to_json;
  EXPECT_TRUE(parser.parse(str));
  const int buffer_size = 128;
  char buffer[buffer_size];
  memcpy(buffer,
         parser.getWriter().getOutput()->getBuffer(),
         (unsigned)parser.getWriter().getOutput()->getSize());

  FbsonUpdater updater(FbsonDocument::createDocument(
                         buffer,
                         (unsigned)parser.getWriter().getOutput()->getSize()),
                       buffer_size);

  FbsonWriter writer;
  FbsonValue *to_be_updated;
  FbsonValueCreater creater;
  // Get the element in the dict
  updater.clearPath();
  updater.pushPathKey("str");
  int16_t i16 = 1234;
  updater.updateValue(creater(i16));
  EXPECT_STREQ("{\"str\":1234,\"key2\":-21555}",
               to_json.json(updater.getRoot()));

  int doc_size = updater.getDocument()->numPackedBytes();
  // Update an int8 to str, and the size should remain: in place update
  updater.clearPath();
  updater.pushPathKey("str");
  int8_t i8 = 8;
  updater.updateValue(creater(i8));
  EXPECT_STREQ("{\"str\":8,\"key2\":-21555}", to_json.json(updater.getRoot()));
  EXPECT_EQ(doc_size, updater.getDocument()->numPackedBytes());

  // Update it to int32 and the size should increase by 2 bytes
  updater.clearPath();
  updater.pushPathKey("str");
  int32_t i32 = 123456;
  updater.updateValue(creater(i32));
  EXPECT_STREQ("{\"str\":123456,\"key2\":-21555}",
               to_json.json(updater.getRoot()));
  EXPECT_EQ(doc_size + 2, updater.getDocument()->numPackedBytes());
  // Update it to a double and the size should increase by 4 bytes
  updater.updateValue(creater(123.0));
  EXPECT_STREQ("{\"str\":123,\"key2\":-21555}",
               to_json.json(updater.getRoot()));
  EXPECT_EQ(doc_size + 6, updater.getDocument()->numPackedBytes());

  // Update to a string
  updater.updateValue(creater("ABCDEFGHIJKLMN"));
  EXPECT_STREQ("{\"str\":\"ABCDEFGHIJKLMN\",\"key2\":-21555}",
               to_json.json(updater.getRoot()));
  doc_size = updater.getDocument()->numPackedBytes();

  // Update to a string less than the original one
  updater.updateValue(creater("ABCDEFG"));
  EXPECT_EQ(doc_size, updater.getDocument()->numPackedBytes());
  EXPECT_STREQ("{\"str\":\"ABCDEFG\",\"key2\":-21555}",
               to_json.json(updater.getRoot()));

  // Update to a string larger than the original one
  updater.updateValue(creater("ABCDEFGHIJ"));
  EXPECT_EQ(doc_size, updater.getDocument()->numPackedBytes());
  EXPECT_STREQ("{\"str\":\"ABCDEFGHIJ\",\"key2\":-21555}",
               to_json.json(updater.getRoot()));

  // Update to a string smaller than the string buffer / 2
  updater.updateValue(creater("ABCD"));
  EXPECT_EQ(doc_size - 10, updater.getDocument()->numPackedBytes());
  EXPECT_STREQ("{\"str\":\"ABCD\",\"key2\":-21555}",
               to_json.json(updater.getRoot()));

  // Update to a string larger than the first one
  updater.updateValue(creater("ABCDEFGHIJKLMNOPQ"));
  EXPECT_EQ(doc_size + 3, updater.getDocument()->numPackedBytes());
  EXPECT_STREQ("{\"str\":\"ABCDEFGHIJKLMNOPQ\",\"key2\":-21555}",
               to_json.json(updater.getRoot()));

  // Update to a dictionary
  writer.reset();
  writer.writeStartObject();
  writer.writeKey("KEY1");
  writer.writeStartString();
  writer.writeString("ABCDEFG");
  writer.writeEndString();
  writer.writeKey("SOMETHING");
  writer.writeInt(12345);
  writer.writeEndObject();
  updater.clearPath();
  updater.pushPathKey("key2");
  updater.updateValue(
    FbsonDocument::createValue(
      writer.getOutput()->getBuffer(),
      (int)writer.getOutput()->getSize()));
  EXPECT_STREQ("{\"str\":\"ABCDEFGHIJKLMNOPQ\","\
               "\"key2\":{\"KEY1\":\"ABCDEFG\",\"SOMETHING\":12345}}",
               to_json.json(updater.getRoot()));
  // Update to an array
  writer.reset();
  writer.writeStartArray();
  writer.writeInt(123);
  writer.writeNull();
  writer.writeBool(true);
  writer.writeEndArray();
  updater.clearPath();
  updater.pushPathKey("str");
  updater.updateValue(FbsonDocument::createValue(
                   writer.getOutput()->getBuffer(),
                   (int)writer.getOutput()->getSize()));
  EXPECT_STREQ("{\"str\":[123,null,true],"\
               "\"key2\":{\"KEY1\":\"ABCDEFG\",\"SOMETHING\":12345}}",
               to_json.json(updater.getRoot()));

}
