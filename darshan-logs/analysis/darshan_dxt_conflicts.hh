#ifndef DARSHAN_DXT_CONFLICTS_HH
#define DARSHAN_DXT_CONFLICTS_HH

#include <set>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>


class Event {
public:
  int rank;
  enum Mode {READ, WRITE} mode;
  enum API {POSIX, MPI} api;
  int64_t offset, length;
  double start_time, end_time;

  Event(int rank_) : rank(rank_) {}

  Event(int rank_, enum Mode mode_, enum API api_,
        int64_t offset_, int64_t length_,
        double start_time_, double end_time_)
    : rank(rank_), mode(mode_), api(api_), offset(offset_), length(length_),
      start_time(start_time_), end_time(end_time_) {}

  std::string str() const {
    std::ostringstream buf;
    buf << "rank " << rank
        << " bytes " << offset << ".." << (offset+length-1)
        << " " << (api==POSIX ? "POSIX" : "MPIIO")
        << " " << (mode==READ ? "read" : "write")
        << " time " << std::fixed << std::setprecision(4) << start_time << ".." << end_time;
    return buf.str();
  }    
  
  bool overlaps(const Event &other) const {
    return (offset < (other.offset + other.length))
      && ((offset+length) > other.offset);
  }

  /* If all accesses are done in terms of blocks of data, set this to
     the block size so overlaps can be computed correctly.

     For example, let block_size be 100. Then every read or write to disk
     occurs in blocks of 100 bytes. If P0 wants to overwrite bytes 0..3, it
     will need to read bytes 0..99 from disk, overwrite the first four bytes,
     then write bytes 0..99 to disk. If P1 writes bytes 96..99 with no
     synchronization, it may complete its operation after P0 read the block
     and before P0 wrote the block. Then when P0 writes its block, it will
     overwrite P1's changes.

     AFAICT, this will only be an issue in write-after-write (WAW) situations.
     In RAW or WAR situations, if the byte range doesn't actually overlap, the
     read will get the same result whether preceding write completed or not.
  */     
  static int block_size;
  static void setBlockSize(int b) {block_size = b;}

  bool overlapsBlocks(const Event &other) const {
    int64_t this_start, this_end, other_start, other_end;

    this_start = blockStart(offset);
    this_end = blockEnd(offset + length - 1);

    other_start = blockStart(other.offset);
    other_end = blockEnd(other.offset + other.length - 1);
  
    return (this_start < other_end) && (this_end > other_start);
  }    

  // round down an offset to the beginning of a block
  static int64_t blockStart(int64_t offset) {
    return offset - (offset % block_size);
  }

  // round up an offset to the end of a block
  static int64_t blockEnd(int64_t offset) {
    return blockStart(offset) + block_size - 1;
  }

  // order by offset and then start time
  bool operator < (const Event &that) const {
    if (offset == that.offset) {
      return start_time < that.start_time;
    } else {
      return offset < that.offset;
    }
  }
};


class File {
public:
  const std::string id;  // a hash of the filename generated by Darshan
  const std::string name;

  // ordered by offset
  typedef std::set<Event> EventSetType;
  EventSetType events;

  File(const std::string &id_, const std::string &name_) : id(id_), name(name_) {}
};


#endif // DARSHAN_DXT_CONFLICTS_HH
