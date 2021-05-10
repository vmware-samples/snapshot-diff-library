/*
 * Copyright 2020-2021 VMware, Inc.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <iostream>

#include "snapshot_diff.h"

using namespace std;

int main(int argc, char** argv)
{
   if (argc != 5) {
      cerr << "Invalid number of args to snapshot-diff" << endl;
      cerr << "Usage : " << argv[0] << " snapdir-path snap1 snap2 resultdir-path" << endl;
      return 1;
   }

   if (GetSnapshotDiff(argv[1], argv[2], argv[3], argv[4], true) != 0) {
      cerr << "Snapshot diff operation failed, please check log file for details" << endl;
      return 1;
   }

   cout << "Snapshot diff operation completed sucessfully, result exported to "
        << string(argv[4]) << endl;

   return 0;
}
