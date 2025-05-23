# dingofs_tool

A tool for DingoFS.

Usage:

```shell
dingofs_tool [Command] [OPTIONS...]
```

When you are not sure how to use a command, --example can give you an example of usage:

```shell
dingofs_tool [Command] --example
```

For example:

```shell
$ dingofs_tool status-mds --example
Example :
dingofs_tool status-mds [-confPath=/etc/dingofs/tools.conf] [-rpcTimeoutMs=5000] [-rpcRetryTimes=5] [-mdsAddr=127.0.0.1:16700,127.0.0.1:26700]
```

In addition, this tool reads the configuration from /etc/dingofs/tools.conf,
and can be specified by "-confPath=".

---

## Table of Contents

* [version](#version)
* [status](#status)
* [list](#list)
* [create](#create)
* [umount](#umount)
* [usage](#usage)
* [delete](#delete)
* [check](#check)
* [query](#query)

---

## version

### **version**

show the version of dingofs_tool

Usage:

```shell
dingofs_tool version
```

Output:

```shell
1.0.0
```

[TOC](#table-of-contents)

---

## status

### **status**

show the status of cluster, include mds, metaserver and copyset.

Usage:

```Shell
dingofs_tool status
```

Output:
```shell
mds version: 1.0.0
leader mds: 192.168.1.1:36700
standby mds: [ 192.168.1.1:16700 192.168.1.1:26700 ].

[metaserver]
metaserver version: 1.0.0
online metaserver: [ 192.168.1.1:16701 192.168.1.1:26701 192.168.1.1:36701 ].

[etcd]
etcd version: 3.4.0
leader etcd: 192.168.1.1:32379
standby etcd: [ 192.168.1.1:12379 192.168.1.1:22379 ].

[copyset]
all copyset is health.

[cluster]
cluster is healthy!
```

### **status-mds**

show the status of mds who are specified by mdsAddr in the configuration file

Usage:

```shell
dingofs_tool status-mds
```

Output:

```shell
mds version: 1.0.0
leader mds: 192.168.1.1:16700
standy mds: [ ].
offline mds: [ 192.168.1.1:27700 192.168.1.1:36700 ].
```

### **status-metaserver**

show the status of metaserver who are specified by metaserverAddr in the configuration file

Usage:

```shell
dingofs_tool status-metaserver
```

Output:

```shell
metaserver version: 1.0.0
online metaserver: [ 192.168.1.1:16701 192.168.1.1:36701 ].
offline metaserver: [ 192.168.1.1:26701 ].
```

### **status-etcd**

show the status of metaserver who are specified by etcdAddr in the configuration file

Usage:

```shell
dingofs_tool status-etcd
```

Output:

```shell
etcd version: 3.4.0
leader etcd: 192.168.1.1:12379
standy etcd: [ 192.168.1.1:22379 ].
offline etcd: [ 192.168.1.1:32379 ].
```

### **status-copyset**

show the status of copyset

Usage:

```shell
dingofs_tool status-copyset
```

Output:

```shell
all copyset is health.
copyset[4294967297]:
-info:
statusCode: TOPO_OK copysetInfo { poolId: 1 copysetId: 1 peers { id: 1 address: "192.168.1.1:36701:0" } peers { id: 2 address: "192.168.1.1:26701:0" } peers { id: 3 address: "192.168.1.1:16701:0" } epoch: 0 leaderPeer { id: 3 address: "192.168.1.1:16701:0" } }
-status:
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 1 peer { address: "192.168.1.1:16701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 1 lastIndex: 1 diskIndex: 1 epoch: 0 }
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 4 peer { address: "192.168.1.1:26701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 1 lastIndex: 1 diskIndex: 1 epoch: 0 }
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 4 peer { address: "192.168.1.1:36701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 1 lastIndex: 1 diskIndex: 1 epoch: 0 }

...

```

[TOC](#table-of-contents)

---

## list

### **list-fs**

list all fs in cluster

Usage:

```shell
dingofs_tool list-fs
```

Output:

```shell
fsId: 1
fsName: "/test"
status: INITED
rootInodeId: 1
capacity: 18446744073709551615
blockSize: 1
mountNum: 1
mountpoints: "09a03e7f5ece:/usr/local/dingofs/client/mnt"
fsType: TYPE_S3
detail {
  s3Info {
    ak: "********************************"
    sk: "********************************"
    endpoint: "********************************"
    bucketname: "********************************"
    blockSize: 1048576
    chunkSize: 4194304
  }
}

...

```

### **list-coysetInfo**

list all copysetInfo in cluster

Usage:

```shell
dingofs_tool list-copysetInfo
```

Output:

```shell
copyset[4294967297]:
statusCode: TOPO_OK
copysetInfo {
  poolId: 1
  copysetId: 1
  peers {
    id: 1
    address: "192.168.1.1:36701:0"
  }
  peers {
    id: 2
    address: "192.168.1.1:26701*:0"
  }
  peers {
    id: 3
    address: "192.168.1.1:16701:0"
  }
  epoch: 0
  leaderPeer {
    id: 3
    address: "192.168.1.1:16701:0"
  }
}

...

```

### **list-topology**

list cluster's topology

Usage:

```shell
dingofs_tool list-topology
```

Output:

```shell
[cluster]
clusterId: 0900e83d-eb1f-45d0-9e42-ba89e3776fa7
[pool]
poolId:1, poolName:pool1, createTime:1640187638, policy:{ copysetNum:100 replicaNum:3 zoneNum:3 }, zoneList:{ 3 2 1 }
[zone]
zoneId:1, zoneName:zone1, poolId:1 serverList:{ 1 }
zoneId:2, zoneName:zone2, poolId:1 serverList:{ 2 }
zoneId:3, zoneName:zone3, poolId:1 serverList:{ 3 }
[server]
serverId:1, hostname:192.168.1.1_0_0, internalIp:192.168.1.1, internalPort:16701, externalIp:192.168.1.1, externalPort:16701, zoneId:1, poolId:1 metaserverList:{ 1 }
serverId:2, hostname:192.168.1.1_1_0, internalIp:192.168.1.1, internalPort:26701, externalIp:192.168.1.1, externalPort:26701, zoneId:2, poolId:1 metaserverList:{ 3 }
serverId:3, hostname:192.168.1.1_2_0, internalIp:192.168.1.1, internalPort:36701, externalIp:192.168.1.1, externalPort:36701, zoneId:3, poolId:1 metaserverList:{ 2 }
[metaserver]
metaserverId:1, hostname:dingofs-metaserver, hostIp:192.168.1.1, port:16701, externalIp:192.168.1.1, externalPort:16701, onlineState:ONLINE, serverId:1
metaserverId:2, hostname:dingofs-metaserver, hostIp:192.168.1.1, port:36701, externalIp:192.168.1.1, externalPort:36701, onlineState:ONLINE, serverId:3
metaserverId:3, hostname:dingofs-metaserver, hostIp:192.168.1.1, port:26701, externalIp:192.168.1.1, externalPort:26701, onlineState:ONLINE, serverId:2
```

> You can use -jsonType and -jsonPath to output the topology to a json file.
>
> The parameter -jsonType supports build and tree.
>
> build: can be used to create the same topology as the current one;
>
> tree: build a json tree of the current topology.
>
> Usage:
>
>```shell
> dingofs_tool list-topology -jsonPath=/tmp/test.json -jsonType=build | cat /tmp/test.json | jq .
>```
> Output:
>```
>{
>  "pools": [
>    {
>      "copysetnum": 100,
>      "name": "defaultPool",
>      "replicasnum": 3,
>      "zonenum": 3
>    }
>  ],
>  "servers": [
>    {
>      "externalip": "192.168.1.1",
>      "externalport": 16701,
>      "internalip": "192.168.1.1",
>      "internalport": 16701,
>      "name": "192.168.1.1_0_0",
>      "pool": "defaultPool",
>      "zone": "zone1"
>    },
>    {
>      "externalip": "192.168.1.1",
>      "externalport": 26701,
>      "internalip": "192.168.1.1",
>      "internalport": 26701,
>      "name": "192.168.1.1_1_0",
>      "pool": "defaultPool",
>      "zone": "zone2"
>    },
>    {
>      "externalip": "192.168.1.1",
>      "externalport": 36701,
>      "internalip": "192.168.1.1",
>      "internalport": 36701,
>      "name": "192.168.1.1_2_0",
>      "pool": "defaultPool",
>      "zone": "zone3"
>    }
>  ]
>}
>```
>

[TOC](#table-of-contents)

---

## create

### **create-topology**

build cluster topology

Usage:

```shell
dingofs_tool create-topology
```

Output:

```shell

```

***if success, there is no output.**  

### **create-fs**

Usage:

```shell
dingofs_tool create-fs
```

Output:

```shell
create fs success.
the create fs info is:
fsId: 3 fsName: "/test" status: INITED rootInodeId: 1 capacity: 18446744073709551615 blockSize: 1048576 mountNum: 0 fsType: TYPE_S3 detail { s3Info { ak: "ak" sk: "sk" endpoint: "endpoint" bucketname: "bucket" blockSize: 1048576 chunkSize: 4194304 } }
```

***If all the parameters of fs are the same as the existing ones, success will still be returned.**

***Some parameters of create-fs such as s3_sk, s3_ak are named s3.sk, sk.ak in the configuration file (tool.conf).**

***Please pay attention.**

[TOC](#table-of-contents)

---

## umount

### **umount-fs**

umount fs from cluster

Usage:

```shell
dingofs_tool umount-fs
```

Output:

```shell
umount fs from cluster success.
```

[TOC](#table-of-contents)

---

## usage

### **usage-metadata**

show the metadata usage of cluster

Usage：

```shell
dingofs_tool usage-metadata
```

Output:

```shell
metaserver[192.168.1.1:26701 usage: total: 1.06 TB used: 755.74 GB left: 327.23 GB
metaserver[192.168.1.1:16701 usage: total: 1.06 TB used: 755.74 GB left: 327.23 GB
metaserver[192.168.1.1:36701 usage: total: 1.06 TB used: 755.74 GB left: 327.23 GB
all cluster usage: total: 3.17 TB used: 2.21 TB left: 981.68 GB
```

[TOC](#table-of-contents)

---

## delete

### **delete-fs**

delete fs by fsName

Usage：

```shell
dingofs_tool delete-fs -fsName=/test
```

Output:

```shell
This command will delete fs (test) and is not recoverable!!!
Do you really want to delete this fs? [Yes, delete!]: Yes, delete!
delete fs (test) success.
```

[**WARNING**] If you enter -noconfirm, fs will be deleted directly without checking, please use with caution!

Usage：

```shell
dingofs_tool delete-fs -fsName=/test -noconfirm
```

Output:

```shell
delete fs (test) success.
```

[TOC](#table-of-contents)

---

## check

### **check-copyset**

checkout copyset status

Usage:

```shell
dingofs_tool checkout-copyset -copysetId=10 -poolId=1
```

Output:

```shell
copyset[4294967306]:
state: 1
peer {
  address: "192.168.1.1:16701:0"
}
leader {
  address: "192.168.1.1:16701:0"
}
readonly: false
term: 2
committedIndex: 1
knownAppliedIndex: 1
pendingIndex: 0
pendingQueueSize: 0
applyingIndex: 0
firstIndex: 2
lastIndex: 1
diskIndex: 1
epoch: 0
```

[TOC](#table-of-contents)

---

## query

### **query-copyset**

query copyset by copysetId

Usage:

```shell
dingofs_tool query-copyset -copysetId=10 -poolId=1
```

Output:

```shell
copyset[4294967306]:
-info:
statusCode: TOPO_OK copysetInfo { poolId: 1 copysetId: 10 peers { id: 1 address: "192.168.1.1:36701:0" } peers { id: 2 address: "192.168.1.1:26701:0" } peers { id: 3 address: "192.168.1.1:16701:0" } epoch: 0 leaderPeer { id: 3 address: "192.168.1.1:16701:0" } }
```

When using the -detail parameter, you can get information about copyset status.

```shell
copyset[4294967306]:
-info:
statusCode: TOPO_OK copysetInfo { poolId: 1 copysetId: 10 peers { id: 1 address: "192.168.1.1:36701:0" } peers { id: 2 address: "192.168.1.1:26701:0" } peers { id: 3 address: "192.168.1.1:16701:0" } epoch: 0 leaderPeer { id: 3 address: "192.168.1.1:16701:0" } }
-status:
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 1 peer { address: "192.168.1.1:16701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 2 lastIndex: 1 diskIndex: 1 epoch: 0 }
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 4 peer { address: "192.168.1.1:26701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 2 lastIndex: 1 diskIndex: 1 epoch: 0 }
status: COPYSET_OP_STATUS_SUCCESS copysetStatus { state: 4 peer { address: "192.168.1.1:36701:0" } leader { address: "192.168.1.1:16701:0" } readonly: false term: 2 committedIndex: 1 knownAppliedIndex: 1 pendingIndex: 0 pendingQueueSize: 0 applyingIndex: 0 firstIndex: 2 lastIndex: 1 diskIndex: 1 epoch: 0 }
```

### **query-partition**

query copyset in partition by partitionId

Usage:

```shell
dingofs_tool query-partition -query-partition -partitionId=1
```

Output:

```shell
statusCode: TOPO_OK
copysetMap {
  key: 1
  value {
    poolId: 1
    copysetId: 8
    peers {
      id: 1
      address: "192.168.1.1:36701:0"
    }
    peers {
      id: 2
      address: "192.168.1.1:26701:0"
    }
    peers {
      id: 3
      address: "192.168.1.1:16701:0"
    }
  }
}
```

### **query-metaserver**

query metaserver by metaserverId or metaserverName

Usage:

```shell
dingofs_tool query-metaserver -metaserverId=1
```

Output:

```shell
statusCode: TOPO_OK
MetaServerInfo {
  metaServerID: 1
  hostname: "******************"
  hostIp: "192.168.1.1"
  port: 36701
  externalIp: "192.168.1.1"
  externalPort: 36701
  onlineState: ONLINE
}
```

### query-fs

query fs by fsId or fsName, fsId first.

Usage:

```shell
dingofs_tool query-fs -fsId=1
```

Output:

```shell
fsId: 1
fsName: "/test"
status: INITED
rootInodeId: 1
capacity: 18446744073709551615
blockSize: 1
mountNum: 1
mountpoints: "09a03e7f5ece:/usr/local/dingofs/client/mnt"
fsType: TYPE_S3
detail {
  s3Info {
    ak: "********************************"
    sk: "********************************"
    endpoint: "********************************"
    bucketname: "********************************"
    blockSize: 1048576
    chunkSize: 4194304
  }
}

```

[TOC](#table-of-contents)

---
