/*
 * Copyright 2020-2021 VMware, Inc.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef __SNAPSHOT_DIFF_H__
#define __SNAPSHOT_DIFF_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int GetSnapshotDiff(const char *snapdir,
                    const char *snap1,
                    const char *snap2,
                    const char *resultdir,
                    bool        genJsonOutput);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __SNAPSHOT_DIFF_H__ */
