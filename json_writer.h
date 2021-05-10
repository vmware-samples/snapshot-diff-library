/*
 * Copyright 2020-2021 VMware, Inc.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __JSON_WRITER_H__
#define __JSON_WRITER_H__

#include <map>
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>

class JsonObject {
public:
   virtual ~JsonObject() {}
   virtual std::ostream& Dump(std::ostream& s) = 0;
   inline std::ostream& DumpStr(std::ostream& s, const std::string& str) {
      s << "\"" << str << "\"";
      return s;
   }
};

typedef std::unique_ptr<JsonObject> JsonObjectPtr;

class JsonNumber : public JsonObject {
public:
   JsonNumber(long long val)
      : val_(val)
   {}

   std::ostream& Dump(std::ostream& s) {
      s << val_;
      return s;
   }

private:
   long long val_;
};

class JsonBool : public JsonObject {
public:
   JsonBool(bool val)
      : val_(val)
   {}

   std::ostream& Dump(std::ostream& s) {
      s << (val_ ? "true" : "false");
      return s;
   }

private:
   bool val_;
};

class JsonString : public JsonObject {
public:
   JsonString(const std::string& val)
      : val_(val)
   {}

   std::ostream& Dump(std::ostream& s) {
      return DumpStr(s, val_);
   }

private:
   std::string val_;
};

class JsonMap : public JsonObject, std::map<std::string, JsonObjectPtr> {
public:
   template <class T>
   bool Add(std::string const& key, T const val) {
      if (std::is_convertible<T, JsonObject *>::value) {
         this->insert(std::make_pair(key, JsonObjectPtr(val)));
      } else {
         return false;
      }
      return true;
   }

   std::ostream& Dump(std::ostream& s) {
      s << "{\n";
      for (const auto& e: *this) {
         // Json doesn't allow trailing comma, so only
         // append comma to previous line if we are appending
         if (*this->begin() != e) s << "," << std::endl;
         DumpStr(s, e.first) << " : ";
         e.second->Dump(s);
      }
      s << "\n}";
      return s;
   }
};

class JsonArray : public JsonObject, std::vector<JsonObjectPtr> {
public:
   using std::vector<JsonObjectPtr>::push_back;
   using std::vector<JsonObjectPtr>::size;

   std::ostream& Dump(std::ostream& s) {
      s << "[\n";
      for (const auto& e: *this) {
         if (*this->begin() != e) s << "," << std::endl;
         e->Dump(s);
      }
      s << "\n]";
      return s;
   }
};

#endif /* __JSON_WRITER_H__ */
