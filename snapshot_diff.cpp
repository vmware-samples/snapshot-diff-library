/*
 * Copyright 2020-2021 VMware, Inc.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if !defined(_WIN32) && !defined(__linux__)
#error Unsupported platform
#endif

#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <time.h>
#include <vector>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
const std::string separator("\\");
#include <direct.h>
#include <windows.h>
#else
const std::string separator("/");
#endif /* _WIN32 */

#include "snapshot_diff.h"
#include "json_writer.h"

#define BUFSIZE (16<<10)
#define MAX_RETRIES 10

using namespace std;

#define LOG_INFO   logFile << GetTime() << " INFO: "
#define LOG_ERROR  logFile << GetTime() << " ERROR: "

typedef map <int, fstream*> BucketFileMap;

/*
 *------------------------------------------------------------------------
 *
 * GetTime --
 *
 *      Returns current timestamp in string form
 *
 * Results:
 *      String representing current time
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

static string
GetTime()
{
   char buffer[32];
   time_t now = time(0);
   tm* gmt = gmtime(&now);
   strftime(buffer,32,"%Y-%m-%dT%H:%M:%S", gmt);
   return buffer;
}


/*
 *------------------------------------------------------------------------
 *
 * IsDirEmpty --
 *
 *      Returns if path directory is empty
 *
 * Results:
 *      true if path exists and is directory, false otherwise
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

static bool
IsDirEmpty(const string& dirPath)
{
   struct dirent *dp;
   int dircount;
   DIR *dir = opendir(dirPath.c_str());

   for (dircount = 0; dircount < 3; ++dircount) {
      dp = readdir(dir);
   }
   closedir(dir);

   return dp == NULL;
}


/*
 *------------------------------------------------------------------------
 *
 * IsDir --
 *
 *      Returns if path exists and is directory
 *
 * Results:
 *      true if path exists and is directory, false otherwise
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

bool
IsDir(const string& dirPath)
{
#ifdef _WIN32
   struct _stat s;

	if (_stat(dirPath.c_str(), &s) == 0) {
      if(s.st_mode & S_IFDIR) {
         return true;
      }
   }
#elif __linux__
   struct stat s;

   if(stat(dirPath.c_str(), &s) == 0) {
      if(s.st_mode & S_IFDIR) {
         return true;
      }
   }
#endif
   return false;
}


/*
 *------------------------------------------------------------------------
 *
 * MkDir --
 *
 *      Executes the system appropriate version of mkdir
 *
 * Results:
 *      The return value of mkdir or _mkdir
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

int
MkDir(const string& dirPath)
{
#ifdef _WIN32
   return _mkdir(dirPath.c_str());
#elif __linux__
   return mkdir(dirPath.c_str(), 0777);
#endif
}


/*
 *------------------------------------------------------------------------
 *
 * OpenStreamUnreliable --
 *
 *     Open snapdiff stream based on given file name. There is a chance
 *     of snapdiff open failing due to buffer size issues. We can retry
 *     opening if this is the case.
 *
 * Results:
 *     0 on successful open, 1 on failure (exceeds maximum retry)
 *
 * Side effects:
 *      snapDiffFile will contain file stream of snapdiff
 *
 *------------------------------------------------------------------------
 */

int
OpenStreamUnreliable(ifstream &snapDiffFile,
                     const string snapDiffFileName,
                     ofstream& logFile)
{
   int numRetries = 0;

   LOG_INFO << "Opening snapdiff stream: " + snapDiffFileName << endl;

   do {
      snapDiffFile.open(snapDiffFileName.c_str(), std::ifstream::in);

      if (!snapDiffFile.is_open()) {
         LOG_ERROR << "Snapshot diff not opened: " + snapDiffFileName << ", retrying...(" << numRetries << ")" << endl;
         LOG_ERROR << "Operation returned " << strerror(errno) << endl;
         if (errno != ENOENT) {
            break;
         }
      } else {
         break;
      }
   } while (numRetries++ < MAX_RETRIES);

   if (!snapDiffFile.is_open()) {
      LOG_ERROR << "Could not open snapshot diff: " + snapDiffFileName << endl;
      LOG_ERROR << "Error: " << strerror(errno) << endl;
      return 1;
   }

   return 0;
}


/*
 *------------------------------------------------------------------------
 *
 * ReadRawDiff --
 *
 *      Reads all diff chunk/pages between two snapshots and places them in the
 *      raw directory. Diff pages are named into file 0, 1, 2, ... etc.
 *
 * Results:
 *      On success: the number of diff pages read
 *      On failure: -1
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

int
ReadRawDiff(const string& snapDir,
            const string& snap1,
            const string& snap2,
            const string& rawDir,
            ofstream&     logFile)
{
   bool eof = false;
   int readNum = 0;
   string startPoint = "0";
   string buf;
   buf.resize(BUFSIZE);
   int numRetryReads = 0;

   while (!eof) {
      string diffLine;

#ifdef _WIN32
      const string diffFileName = snapDir + ":snapdiff." + snap1 + "^"
         + snap2 + "^" + startPoint;
#elif __linux__
      const string diffFileName = snapDir + separator + snap1 + "^"
         + snap2 + "^" + startPoint;
#endif

      ifstream snapDiffFile;

      if (OpenStreamUnreliable(snapDiffFile, diffFileName, logFile) == 1) {
         return -1;
      }

      // Store snapshot diff data on local system.
      auto localFileName = rawDir + separator + to_string(readNum);

      fstream localFile{localFileName,
         fstream::in | fstream::out | fstream::trunc};
      if (!localFile.is_open()) {
         LOG_ERROR << "Could not open file: " + localFileName << endl;
         return -1;
      }

      LOG_INFO << "Saving raw chunk in file: " + localFileName << endl;
      LOG_INFO << "Reading snapdiff: " + diffFileName << endl;

      int nread;
      bool statusBad;

      do {
         snapDiffFile.read(&buf[0], BUFSIZE);
         if ((statusBad = snapDiffFile.bad())) {
            break;
         }
         nread = snapDiffFile.gcount();
         localFile.write(buf.c_str(), nread);
      } while(nread > 0);

      snapDiffFile.close();

      /** There is a chance of snapdiff read failing due to buffer size
      issues. We can retry open and read if this is the case. **/
      if (statusBad) {
         if (numRetryReads == MAX_RETRIES) {
            LOG_ERROR << "Read snapdiff failed: exceeded maximum retries." << endl;
            return -1;
         };
         LOG_ERROR << "Reading snapdiff stream returned bad: " << diffFileName
               << ", reopening and retrying...(" << numRetryReads << ")" << endl;

         ++numRetryReads;
         continue;
      }

      localFile.seekg(0, fstream::beg);

      while (getline(localFile, diffLine, '\n')) {
         istringstream lineStream{diffLine};
         string s;
         string currCookie;

         for (int i = 0; i < 3; ++i) {
            currCookie = s;
            lineStream >> s;
         }

         if (s == "EOB") {
            localFile.close();
            ++readNum;
         } else if (s == "EOF") {
            localFile.close();
            ++readNum;
            eof = true;
         } else {
            startPoint = currCookie;
         }
      }

      if (!localFile.eof()) {
         LOG_ERROR << "Error reading file: " + localFileName << endl;
         return -1;
      }
   }

   return readNum;
}


/*
 *------------------------------------------------------------------------
 *
 * BucketizeDiff --
 *
 *      Creates buckets folder inside result directory and organizes raw diffs
 *      into buckets
 *
 * Results:
 *      Return true if successful, false otherwise
 *
 * Side effects:
 *      Buckets contains open fstream pointers to a file for each bucket
 *
 *------------------------------------------------------------------------
 */

static bool
BucketizeDiff(BucketFileMap& buckets,
              const string&  rawDir,
              int            readNum,
              const string&  resultDir,
              ofstream&      logFile)
{
   string bucketsDir = resultDir + "/parallel_diff";
   int status = MkDir(bucketsDir.c_str());

   if (status != 0) {
      LOG_ERROR << "Unable to create directory: " + bucketsDir << endl;
      return false;
   }

   for (int fileNum = 0; fileNum < readNum; ++fileNum) {
      string curFileName = rawDir + separator + to_string(fileNum);
      ifstream curFile{curFileName};
      string diffLine;

      if (!curFile.is_open()) {
         LOG_ERROR << "Could not open file: " + curFileName << endl;
         return false;
      }

      LOG_INFO << "Bucketizing diff from raw file: " + curFileName << endl;

      while (getline(curFile, diffLine, '\n')) {
         istringstream lineStream{diffLine};
         string s;
         int level;

         // Normalize level to positive value
         lineStream >> s;
         level = stoi(s) + 513;
         // Seek past level and objId (omit these from final output)
         lineStream >> s;

         if (!buckets.count(level)) {
            auto bucketName = bucketsDir + separator + to_string(level);
            auto openFlags = fstream::in | fstream::out | fstream::trunc;
            auto curBucketFile = std::make_unique<fstream>(bucketName, openFlags);

            if (!curBucketFile->is_open()) {
               LOG_ERROR << "Could not open file: " + bucketName << endl;
               return false;
            }

            LOG_INFO << "Writing to bucket file: " + bucketName << endl;

            buckets[level] = curBucketFile.release();
         }

         // Write remainder of line to bucket file, omit EOB and EOF
         ostringstream outputLine;
         lineStream >> s;
         outputLine << s;

         while (lineStream >> s) {
            outputLine << "\t" << s;
         }

         auto str = outputLine.str();
         if (str != "EOB" && str != "EOF") {
            *buckets[level] << str << endl;
         } else {
            // Ignore any data after EOB/EOF.
            curFile.seekg(0, fstream::end);
         }
      }

      if (!curFile.eof()) {
         LOG_ERROR << "Error reading file: " + curFileName << endl;
      }
   }
   return true;
}


/*
 *------------------------------------------------------------------------
 *
 * SerializeBuckets --
 *
 *      Places diffs in topological order into a single file
 *
 * Results:
 *      Returns true if successful, false otherwise
 *
 * Side effects:
 *      All file pointers in Buckets will be closed, freed, and deleted
 *
 *------------------------------------------------------------------------
 */

static bool
SerializeBuckets(BucketFileMap *buckets,
                 const string&  resultDir,
                 ofstream&      logFile)
{
   string serialDiffFileName = resultDir + separator + "serialized_diff";
   ofstream SerialDiffFile{serialDiffFileName};

   if (!SerialDiffFile.is_open()) {
      LOG_ERROR << "Could not open file: " + serialDiffFileName << endl;
      return false;
   }

   LOG_INFO << "Writing to serialized diff file: " + serialDiffFileName << endl;

   for (auto itr = buckets->begin(); itr != buckets->end(); ++itr) {
      itr->second->seekg(0, fstream::beg);
      SerialDiffFile << itr->second->rdbuf();
      itr->second->close();
      delete itr->second;
      itr->second = nullptr;
   }

   SerialDiffFile.close();
   return true;
}


/*
 *------------------------------------------------------------------------
 *
 * MakeStatsJsonMap --
 *
 *      Appends JsonMap with basic stat information for a new fs entry.
 *
 * Results:
 *      diffItem contains fields for atime, ctime, mtime, size, and path.
 *      Returns true if success, false otherwise
 *
 * Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

static bool
MakeStatsJsonMap(JsonMap      *diffItem,
                 const string& snapDir,
                 const string& path)
{
   string absPath = snapDir + "/../../" + path;
   long long ansec, cnsec, mnsec;
   time_t asec, csec, msec;
#ifdef _WIN32
   struct _stat s;

	if (_stat(absPath.c_str(), &s) == 0) {
      asec = s.st_atime;
      csec = s.st_ctime;
      msec = s.st_mtime;
   } else {
      return false;
   }

   // Windows doesn't report time in nanoseconds,
   // so all nsec fields are set to 0 for Windows
   ansec = 0;
   cnsec = 0;
   mnsec = 0;
#else
   struct stat s;

   if (lstat(absPath.c_str(), &s) == 0) {
      ansec = s.st_atim.tv_nsec;
      asec = s.st_atim.tv_sec;
      cnsec = s.st_ctim.tv_nsec;
      csec = s.st_ctim.tv_sec;
      mnsec = s.st_mtim.tv_nsec;
      msec = s.st_mtim.tv_sec;
   } else {
      return false;
   }
#endif /* _WIN32 */
   auto atime = std::make_unique<JsonMap>();
   auto ctime = std::make_unique<JsonMap>();
   auto mtime = std::make_unique<JsonMap>();

   atime->Add("nsec", new JsonNumber(ansec));
   atime->Add("sec", new JsonNumber(asec));
   ctime->Add("nsec", new JsonNumber(cnsec));
   ctime->Add("sec", new JsonNumber(csec));
   mtime->Add("nsec", new JsonNumber(mnsec));
   mtime->Add("sec", new JsonNumber(msec));

   diffItem->Add("size", new JsonNumber(s.st_size));
   diffItem->Add("atime", atime.release());
   diffItem->Add("ctime", ctime.release());
   diffItem->Add("mtime", mtime.release());
   diffItem->Add("path", new JsonString(path));

   return true;
}


/*
 *------------------------------------------------------------------------
 *
 * GenerateJSON --
 *
 *      Emits serialized diff in JSON format
 *
 * Results:
 *      Returns true if successful, false otherwise
 *
 * Side effects:
 *      Emits JSON file
 *
 *------------------------------------------------------------------------
 */

static bool
GenerateJSON(const string&  snapDir,
             const string&  jsonDir,
             const string&  resultDir,
             ofstream&      logFile)
{
   string serialFileName = resultDir + separator + "serialized_diff";
   ifstream serialFile{serialFileName};
   string diffLine;

   if (!serialFile.is_open()) {
      LOG_ERROR << "Could not open file: " + serialFileName << endl;
      return false;
   }

   LOG_INFO << "JSONizing diffs from: " + serialFileName << endl;

   int jsonFileCount = 0;
   bool done = false;

   while(!done) {
      JsonArray diffItems;

      while (diffItems.size() < 1000) {
         // When number of json items reaches 1000, write to json file
         // to prevent file size from becoming too large. Open the next
         // json file for writing.
         if (!getline(serialFile, diffLine, '\n')) {
            done = true;
            break;
         }

         istringstream lineStream{diffLine};
         string token;
         vector<string> diffLineList;

         while (lineStream >> token) {
            diffLineList.push_back(token);
         }

         string op = diffLineList[0];
         string path = diffLineList[1];
         int split = op.find("_");
         string entrytype = op.substr(0, split);
         string optype = op.substr(split + 1, op.length());
         auto diffItem = std::make_unique<JsonMap>();

         if (entrytype == "FILE" || entrytype == "DIR") {
            if (optype == "DELETE") {
               diffItem->Add("type", new JsonString("delete"));
               diffItem->Add("object_type", new JsonString(entrytype == "FILE" ? "file" : "dir"));
               diffItem->Add("path", new JsonString(path));

               diffItems.push_back(JsonObjectPtr(diffItem.release()));
            } else if (optype == "RENAME") {
               diffItem->Add("type", new JsonString("rename"));
               diffItem->Add("path_old", new JsonString(path));
               diffItem->Add("path_new", new JsonString(diffLineList[2]));

               diffItems.push_back(JsonObjectPtr(diffItem.release()));
            } else {
               if (MakeStatsJsonMap(diffItem.get(), snapDir, path) == false) {
                  LOG_ERROR << "Could not stat file: " + path << endl;
               }

               diffItem->Add("type", new JsonString(entrytype == "FILE" ? "file" : "dir"));
               diffItem->Add("created", new JsonBool(optype.find('C') != string::npos ? "true" : "false"));
               diffItem->Add("modified", new JsonBool(optype.find('M') != string::npos ? "true" : "false"));
               diffItem->Add("stat", new JsonBool(optype.find('S') != string::npos ? "true" : "false"));
               diffItem->Add("xattr", new JsonBool(optype.find('X') != string::npos ? "true" : "false"));

               diffItems.push_back(JsonObjectPtr(diffItem.release()));
            }
         } else if (entrytype == "SYM") {
            if (optype == "DELETE") {
               diffItem->Add("type", new JsonString("delete"));
               diffItem->Add("object_type", new JsonString("symlink"));
               diffItem->Add("path", new JsonString(diffLineList[1]));

               diffItems.push_back(JsonObjectPtr(diffItem.release()));
            } else {
               if (MakeStatsJsonMap(diffItem.get(), snapDir, path) == false) {
                  LOG_ERROR << "Could not stat file: " + path << endl;
               }

               diffItem->Add("type", new JsonString("symlink"));
               if (optype.find('C') != string::npos) {
                  diffItem->Add("created", new JsonBool(true));
                  diffItem->Add("target", new JsonString(diffLineList[2]));
               } else {
                  diffItem->Add("created", new JsonBool(false));
               }
               diffItem->Add("stat", new JsonBool(optype.find('S') != string::npos ? "true" : "false"));

               diffItems.push_back(JsonObjectPtr(diffItem.release()));
            }
         }
      }

      if (diffItems.size() > 0) {
         string jsonFileName = jsonDir + separator + to_string(jsonFileCount) + ".json";
         ofstream jsonDiffFile{jsonFileName};

         if (!jsonDiffFile.is_open()) {
            LOG_ERROR << "Could not open file: " + jsonFileName << endl;
            return false;
         }

         LOG_INFO << "Writing to json file: " + jsonFileName << endl;
         diffItems.Dump(jsonDiffFile);
         jsonDiffFile.close();
      }

      ++jsonFileCount;
   }

   return true;
}


/*
 *------------------------------------------------------------------------
 *
 * GetSnapshotDiff --
 *
 *      Reads diff between snap1 and snap2 and outputs ordered/bucketized
 *      diffs by level
 *
 * Results:
 *      0 if successful, 1 if error occurred
 *
 * Side effects:
 *      diffdir directory created and populated (see README.md)
 *
 *------------------------------------------------------------------------
 */

extern "C" int
GetSnapshotDiff(const char *snapDir,
                const char *snap1,
                const char *snap2,
                const char *resultDir,
                bool        genJsonOutput)
{
   if (!IsDir(resultDir)) {
      cerr << "Result directory " << resultDir << " is not a directory." << endl;
      return 1;
   }

   if (!IsDirEmpty(resultDir)) {
      cerr << "Result directory " << resultDir << " is not empty." << endl;
      return 1;
   }

   string logFileName = resultDir + separator + "out.log";
   ofstream logFile {logFileName.c_str()};

   if (!logFile.is_open()) {
      cerr << "Could not open log file: " << logFileName << endl;
      return 1;
   }

#ifndef _WIN32
   if (!IsDir(snapDir)) {
      LOG_ERROR << "Snapshot directory " << snapDir << " is not a directory." << endl;
      return 1;
   }
#endif

   LOG_INFO << "Input parameters : " << endl;
   LOG_INFO << "snapDir: " << snapDir << endl;
   LOG_INFO << "snap1: " << snap1 << endl;
   LOG_INFO << "snap2: " << snap2 << endl;
   LOG_INFO << "resultDir: " << resultDir << endl;

   string rawDir = resultDir + separator + "raw";
   int status = MkDir(rawDir.c_str());

   if (status != 0) {
      LOG_ERROR << "Unable to create directory: " + rawDir << endl;
      return 1;
   }

   LOG_INFO << "Reading raw diffs" << endl;
   int readNum = ReadRawDiff(snapDir, snap1, snap2, rawDir, logFile);

   if (readNum < 0) {
      LOG_ERROR << "Issue in reading raw diff" << endl;
      return 1;
   }

   BucketFileMap buckets;

   LOG_INFO << "Generating bucketized diffs" << endl;
   if (!BucketizeDiff(buckets, rawDir, readNum, resultDir, logFile)) {
      LOG_ERROR << "Issue in bucketizing diff" << endl;
      return 1;
   }

   LOG_INFO << "Generating serialized diffs" << endl;
   if (!SerializeBuckets(&buckets, resultDir, logFile)) {
      LOG_ERROR << "Issue in serializing diff" << endl;
      return 1;
   }

   string jsonDir = resultDir + separator + "serialized_json";
   status = MkDir(jsonDir.c_str());

   if (status != 0) {
      LOG_ERROR << "Unable to create directory: " + jsonDir << endl;
      return 1;
   }

   if (genJsonOutput) {
      LOG_INFO << "Generating json file" << endl;
      if (!GenerateJSON(snapDir, jsonDir, resultDir, logFile)) {
         LOG_ERROR << "Issue in generalizing json" << endl;
         return 1;
      }
   }

   LOG_INFO << "Snapshot diff completed successfully" << endl;
   return 0;
}
