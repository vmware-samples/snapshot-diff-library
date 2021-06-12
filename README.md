**Introduction**<br/>

Snapshot diff wrapper library uses raw diff exposed by backend VDFS file system
and produces easy to consume diff between 2 snapshots.  It can be directly
integrated with the backup program. The library is compatible with Windows and
Linux. The library also provides snapshot-diff utility to create diff from
command line.

**Interface**<br/>

GetSnapshotDiff(`snapshot dir`, `first snapshot name`, `second snapshot name`, `output dir`, `generate json`)
 - `snapshot dir` is the path to vdfs directory containing all snapshots
 - `output dir` is the path to the directory where the serialized_diff and
   parallel_diff will be stored (see below)
- `generate json` should be set to `true` if json format output is needed.

**Output directory layout**<br/>

`parallel_diff` contains diff items arranged by level (lower level needs
to be run first).
`serialized_diff` contains all diff items in order.
`serialized_json` contains the diff items in json format (details below).
`raw` will contain intermediary fragments of the diff.
```
<output dir>
    |--- serialized_diff
    |--- serialized_json
    |      |--- 0.json
    |      |--- 1.json
    |--- parallel_diff
    |      |--- 0
    |      |--- 514
    |      |--- 515
    |      |--- 1026
    |--- raw
    |      |--- 0
    |      |--- 2
    |      |--- 3
    |      |--- 4
    |--- snapdiff.log

```
**serialized_diff**<br/>

serialized_diff is a file, which contains diff between the two snapshots. The
diff operations listed in the file are required to be applied in sequential
order. The example of the output is mentioned below.

**parallel_diff**<br/>

parallel_diff is a directory, which contains a number of files with numerical
name. These files must be processed in ascending order. However, operations in
a file are independent and can be applied in parallel. Using parallel diff can
result in significantly reduce backup time.

**serialized_json**<br/>

serialized_json is a directory, which contains the serialized diff in json
format. It is chunked into groups of 1000 diffs, starting from `0.json`, and
spills over to `1.json`, etc. In addition to the basic serialized_diff output,
it contains information about the path, size, ctime, mtime,and atime of the
file. This is only generated when `generate json` param is true.
Below is an example output:

_**Note about running on Windows: Windows reports the ctime, mtime, and atime
only in time_t format, which corresponds to the "sec" field in Unix. The "nsec"
field is thus left as 0 in the json output on Windows.**_

**Example serialized_diff**<br/>
```
DIR_C          .vdfs
DIR_RENAME      a/b/c       .vdfs/22
DIR_DELETE      a/b
DIR_S           a
DIR_CS          a/d
FILE_MS         new/test.txt
FILE_CMS        new/foo.txt
FILE_DELETE     new/bar.txt
DIR_RENAME     .vdfs/22  a/d/c
DIR_S           a/d/c
DIR_DELETE     .vdfs
```

**Example serialized_json/0.json**<br/>
```
[{
"type" : "dir",
"atime" : { "nsec" : 723925300, "sec" : 1404894388 },
"ctime" : { "nsec" : 43381506, "sec" : 1406022454 },
"mtime" : { "nsec" : 723925300, "sec" : 1404894388},
"path" : "/dir_foo/dir_bar",
"created": true,
"modified" : false,
"stat": true,
"xattr": false
},
{
"type" : "file",
"atime" : { "nsec" : 723925300, "sec" : 1404894388 },
"ctime" : { "nsec" : 43381506, "sec" : 1406022454 },
"mtime" : { "nsec" : 723925300, "sec" : 1404894388},
"path" : "/dir_foo/dir_bar/file_foo",
"size" : 470604,
"created": true,
"modified" : true,
"stat": true,
"xattr": false
},
{
"type" : "delete",
"path" : "/dir_foo/file_bar"
},
{
"type" : "rename",
"path_old" : "/dir_foo/foo",
"path_new" : "/dir_foo/dir_bar/new_foo"
}
}]
```

**Prerequisite**<br/>
```
make
g++
g++-mingw-w64-x86-64
```

**Building**<br/>
```
make all
```

**Usage**<br/>
```
Linux:
Copy snapshot-diff to NFS client and run
snapshot-diff <snapshot dir> <snap1> <snap2> <result dir>

Windows:
Copy snapshot-diff to Windows client and run
snapshot-diff.exe <snapshot dir> <snap1> <snap2> <result dir>

```
**Developer Certificate of Origin**<br/>

Before you start working with snapshot-diff-library, please read our Developer Certificate of Origin. All contributions to this repository must be signed as described on that page. Your signature certifies that you wrote the patch or have the right to pass it on as an open-source patch.
