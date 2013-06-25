Reliability analysis of ZFS
===========================


The reliability of a file system considerably depends upon how it deals with on-disk data corruption. A file system should ideally be able to detect and recover from all kinds of data corruptions on disk. ZFS is a new filesystem that arrives almost a generation after the introduction of other desktop filesystems like ext and NTFS and makes strong claims about its reliability mechanisms.In this paper we examine how ZFS deals with on disk data corruptions. We use the knowledge of on disk structures of ZFS to perform corruptions on different types of ZFS objects. This ``type aware'' corruption enables us to perform a systematic exploration of the data protection mechanism of ZFS from various types of data corruptions. This paper performs 90 experiments on ten different ZFS disk objects. The results of these corruption experiments give us information about the classes of on disk faults handled by ZFS. We also get a measure of the robustness of ZFS and gain valuable lessons on building  a reliable and robust file system.


