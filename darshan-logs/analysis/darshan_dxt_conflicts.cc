/*
  darshan_dxt_conflicts - reads the output of darshan-dxt-parser (which
  contains per-call data on each read or write) and outputs any conflicts
  found.

  A conflict is when a pair of events A and B are found such that:
   - A and B access the same file (A.file_hash == B.file_hash)
   - A and B came from different processes (A.rank != B.rank)
   - A and B access overlapping byte ranges
     (A.offset < (B.offset+B.length) && ((A.offset+A.length) > B.offset)
   - At least one of the accesses is a write. (A.mode==WRITE || B.mode==WRITE)


  Sample input:

# DXT, file_id: 8515199880342690440, file_name: /mnt/c/Ed/UI/Research/Darshan/conflict_app.out.RAW.POSIX.NONE
# DXT, rank: 0, hostname: XPS13
# DXT, write_count: 10, read_count: 0
# DXT, mnt_pt: /mnt/c, fs_type: 9p
# Module    Rank  Wt/Rd  Segment          Offset       Length    Start(s)      End(s)
 X_POSIX       0  write        0               0         1048576      4.8324      4.8436
 X_POSIX       0  write        1         1048576         1048576      4.8436      4.8534
 X_POSIX       0  write        2         2097152         1048576      4.8534      4.8637
 X_POSIX       0  write        3         3145728         1048576      4.8638      4.8706
 X_POSIX       0  write        4         4194304         1048576      4.8706      4.8769
 X_POSIX       0  write        5         5242880         1048576      4.8769      4.8832
 X_POSIX       0  write        6         6291456         1048576      4.8832      4.8892
 X_POSIX       0  write        7         7340032         1048576      4.8892      4.8944
 X_POSIX       0  write        8         8388608         1048576      4.8944      4.9000
 X_POSIX       0  write        9         9437184         1048576      4.9000      4.9059

# DXT, file_id: 8515199880342690440, file_name: /mnt/c/Ed/UI/Research/Darshan/conflict_app.out.RAW.POSIX.NONE
# DXT, rank: 1, hostname: XPS13
# DXT, write_count: 0, read_count: 10
# DXT, mnt_pt: /mnt/c, fs_type: 9p
# Module    Rank  Wt/Rd  Segment          Offset       Length    Start(s)      End(s)
 X_POSIX       1   read        0               0         1048576      6.8327      6.8392
 X_POSIX       1   read        1         1048576         1048576      6.8392      6.8434
 X_POSIX       1   read        2         2097152         1048576      6.8434      6.8473
 X_POSIX       1   read        3         3145728         1048576      6.8473      6.8513
 X_POSIX       1   read        4         4194304         1048576      6.8513      6.8553
 X_POSIX       1   read        5         5242880         1048576      6.8553      6.8601
 X_POSIX       1   read        6         6291456         1048576      6.8601      6.8639
 X_POSIX       1   read        7         7340032         1048576      6.8639      6.8673
 X_POSIX       1   read        8         8388608         1048576      6.8673      6.8709
 X_POSIX       1   read        9         9437184         1048576      6.8709      6.8747

*/

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <memory>
#include <regex>

#include "darshan_dxt_conflicts.hh"

using namespace std;


int Event::block_size = 1;


// map file_id (the hash of the file path) to File object.
// Use the hash rather than the path, because the path is
// often truncated in Darshan, leading to collisions that would probably
// be avoided when using the 64-bit hash of the full path.
typedef unordered_map<std::string, unique_ptr<File>> FileTableType;

int readDarshanDxtInput(istream &in, FileTableType &file_table);
bool parseEventLine(Event &e, const string &line);
void writeData(const FileTableType &file_table);
void scanForConflicts(File *f);


int main(int argc, char **argv) {
  FileTableType file_table;

  // Event a(0, Event::READ, 0, 100, 1.0, 1.25);
  // cout << a.offset << ".." << (a.offset + a.length - 1) << endl;

  // ifstream inf("sample_dxt_mpiio.txt");
  ifstream inf("sample.dxt");
  readDarshanDxtInput(inf, file_table);
  // readDarshanDxtInput(cin, file_table);

  // writeData(file_table);

  for (auto &file_it : file_table) {
    File *f = file_it.second.get();
    scanForConflicts(f);
  }

  
  return 0;
}


int readDarshanDxtInput(istream &in, FileTableType &file_table) {
  string line;

  regex section_header_re("^# DXT, file_id: ([0-9]+), file_name: (.*)$");
  regex rank_line_re("^# DXT, rank: ([0-9]+),");
  // regex io_event_re("^ *(X_MPIIO|X_POSIX) +[:digit:]+ +([:alpha:]+)");
  smatch re_matches;
  
  while (true) {

    // skip until the beginning of a section is found
    bool section_found = false;
    while (true) {
      if (!getline(in, line)) break;
      if (regex_search(line, re_matches, section_header_re)) {
        section_found = true;
        break;
      }
    }
    if (!section_found) break;
    
    assert(re_matches.size() == 3);
    string file_id_str = re_matches[1];
    string file_name = re_matches[2];

    File *current_file;
    FileTableType::iterator ftt_iter = file_table.find(file_id_str);
    if (ftt_iter == file_table.end()) {
      cout << "First instance of " << file_name << endl;
      current_file = new File(file_id_str, file_name);
      file_table[file_id_str] = unique_ptr<File>(current_file);
    } else {
      current_file = ftt_iter->second.get();
    }
    
    // find the line with the rank id
    bool rank_found = false;
    while (true) {
      if (!getline(in, line)) break;
      if (regex_search(line, re_matches, rank_line_re)) {
        rank_found = true;
        break;
      }
    }
    if (!rank_found) break;

    int rank = stoi(re_matches[1]);

    cout << "reading rank " << rank << " " << file_name << endl;

    // read until a blank line at the end of the section or EOF
    bool is_eof = false;
    while (true) {
      if (!getline(in, line)) break;
      if (line.length() == 0) {
        // cout << "End of section\n";
        break;
      }
      if (line[0] == '#') continue;
      
      EventPtr event(new Event());
      if (!parseEventLine(*event, line)) {
        cerr << "Unrecognized line: " << line << endl;
      } else {
        // cout << event.str() << endl;
        // current_file->events.insert(*event);
        current_file->addEvent(event);
        // cout << current_file->events.size() << endl;
      }
    }

    if (is_eof) break;
  }

  return 0;
}


/*
bool startsWith(const string &s, const char *search_str) {
  size_t search_len = strlen(search_str);
  return s.length() >= search_len
    && 0 == s.compare(0, search_len, search_str);
}
*/


/* 
   Parse a line in the form:
      X_POSIX   1  read    9    4718592     524288   1.2240  1.2261
   Subexpressions:
   1: io library (X_MPIIO or X_POSIX)
   2: rank
   3: direction (write or read)
   4: offset
   5: length
   6: start time
   7: end time
*/
static regex io_event_re("^ *(X_MPIIO|X_POSIX) +([0-9]+) +([a-z]+) +[0-9]+ +([-0-9]+) +([0-9]+) +([0-9.]+) +([0-9.]+)");

bool parseEventLine(Event &event, const string &line) {
  smatch re_matches;

  if (!regex_search(line, re_matches, io_event_re)) return false;
  if (re_matches.size() != 8) return false;

  /*
  cout << "  " << re_matches[1] << " action=" << re_matches[2]
       << " offset=" << re_matches[3]
       << " length=" << re_matches[4]
       << " start=" << re_matches[5]
       << " end=" << re_matches[6]
       << endl;
  */
  
  if (re_matches[1] == "X_POSIX") {
    event.api = Event::POSIX;
  } else if (re_matches[1] == "X_MPIIO") {
    event.api = Event::MPI;
  } else {
    cerr << "invalid library: " << re_matches[1] << endl;
    return false;
  }

  event.rank = stoi(re_matches[2]);

  if (re_matches[3] == "read") {
    event.mode = Event::READ;
  } else if (re_matches[3] == "write") {
    event.mode = Event::WRITE;
  } else {
    cerr << "invalid io access type: " << re_matches[3] << endl;
    return false;
  }

  event.offset = stoll(re_matches[4]);
  event.length = stoll(re_matches[5]);
  event.start_time = stod(re_matches[6]);
  event.end_time = stod(re_matches[7]);

  return true;
}


void writeData(const FileTableType &file_table) {

  for (auto &file_it : file_table) {
    File *f = file_it.second.get();
    cout << "File " << f->name << endl;
    for (auto &event_it : f->events) {
      cout << event_it.str() << endl;
    }
  }
  /*
  for (FileTableType::const_iterator it=file_table.begin();
       it != file_table.end(); it++) {
    const File *f = it->second.get();
    cout << "File " << f->name << endl;
  }
  */
}


/* Scan through the events, which are ordered by starting byte offset.

   Maintain a list of events, ordered by ending byte offset, with
   no more than one per rank, that represents the ranks that have
   accessed the current byte offset.
   
   If an overlapping event from the same rank is found, it is likely
   an instance of an MPI-IO being implemented with a POSIX call.  The
   MPI-IO call should be a superset of the POSIX call in byte range,
   time range, and operation (write > read). If not, report the
   unexpected situation on stderr.
*/
void scanForConflicts(File *f) {
  // typedef set<const Event*,EventCompareEndOffset> CurrentEventsType;
  // CurrentEventsType current_events;
  // CurrentEventsType::iterator cur_it;

  cout << "scanForConflicts(" << f->name << ")\n";
  // std::map<int,EventSequencePtr>::iterator seq_it;
  // std::map<int,unique_ptr<EventSequence>>::iterator it;
  // for (it = f->rank_seq.begin(); it != f->rank_seq.end(); it++) {
  for (auto &it : f->rank_seq) {
    int rank = it.first;
    EventSequence *seq = it.second.get();
    cout << "  rank " << rank << ", "
         << seq->elist.size() << endl;
  }
  
  /*
  File::EventSetType::iterator ev_it;
  for (ev_it = f->events.begin(); ev_it != f->events.end(); ev_it++) {
    // cout << ev_it->str() << endl;
    const Event &e = *ev_it;
    cout << e.str() << endl;
  }
  */

  // Maintain a collection of events that overlap the current byte offset.
  // vector<Event> state;
  OverlapSet overlap_set;
  
  for (const Event &e : f->events) {
    // cout << e.str() << endl;

    // throw out events that end before the first block of event e
    overlap_set.removeOldEvents(Event::blockStart(e.offset));

    // if e overlaps any events and is the same rank, combine
    // the two.
    if (overlap_set.mergeEventsSameRank(e)) {
      continue;
    }
    
    // if e overlaps any events and is a different rank, 
    // report each overlap
    overlap_set.reportOverlaps(e);

    // if e doesn't overlap any events, but it is a write and shares a
    // block with a write, report WAW false sharing
    overlap_set.reportBlockOverlaps(e);

    // add e to the set of active events
    overlap_set.addEvent(e);
    
  }
  
  
}
