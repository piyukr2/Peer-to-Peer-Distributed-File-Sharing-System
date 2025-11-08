# Detailed Technical Write-Up: Distributed P2P File Sharing System
## Complete Implementation, Control Flow, and Architecture Guide

---

## Table of Contents

**PART 1: FUNDAMENTALS (Understanding the Basics)**

1. [System Overview](#1-system-overview)
2. [Architecture Components](#2-architecture-components)
3. [Data Structures and Storage](#3-data-structures-and-storage)
4. [Network Protocols](#4-network-protocols)

**PART 2: CORE MODULES (Building Blocks)**

5. [Common Module (Shared Utilities)](#5-common-module-shared-utilities)
6. [Tracker Module](#6-tracker-module)
7. [Client Module](#7-client-module)

**PART 3: SYSTEM INTERACTIONS (How Components Work Together)**

8. [Control Flow Between Components](#8-control-flow-between-components)
9. [Feature Implementation Details](#9-feature-implementation-details)

**PART 4: FUNCTION REFERENCE (Deep Dive)**

10. [Complete Function Reference](#10-complete-function-reference)
11. [Feature-to-Function Mapping](#11-feature-to-function-mapping)
12. [Function Call Relationships](#12-function-call-relationships)
13. [How Functions Collaborate to Implement Features](#13-how-functions-collaborate-to-implement-features)

**PART 5: ADVANCED TOPICS**

14. [Threading and Concurrency](#14-threading-and-concurrency)
15. [Error Handling and Recovery](#15-error-handling-and-recovery)

---

## 1. System Overview

### 1.1 Purpose
This system implements a **distributed peer-to-peer (P2P) file sharing network** where users can:
- Register and authenticate
- Create and join groups
- Share files within groups
- Download files directly from other peers (not through trackers)
- Maintain data consistency across multiple tracker servers

### 1.2 High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    DISTRIBUTED P2P SYSTEM                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐         SYNC        ┌──────────────┐      │
│  │   Tracker 0  │ ◄─────────────────► │   Tracker 1  │      │
│  │  (Port 8000) │                     │  (Port 8001) │      │
│  └──────┬───────┘                     └───────┬──────┘      │
│         │                                     │             │
│         │ Client-Tracker Protocol (TCP)       │             │
│         │                                     │             │
│  ┌──────▼───────┐                      ┌──────▼───────┐     │
│  │   Client A   │                      │   Client B   │     │
│  │  (Peer Port) │                      │  (Peer Port) │     │
│  └──────┬───────┘                      └──────┬───────┘     │
│         │                                     │             │
│         └────────────── P2P Protocol ─────────┘             │
│                   (Direct TCP Connection)                   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Key Characteristics:**
- **Centralized Metadata**: Trackers store only metadata (file info, peer addresses), not actual file data
- **Decentralized Transfer**: Files are transferred directly between peers (P2P)
- **Multi-Tracker**: Two synchronized tracker servers for high availability
- **Piece-Based**: Files split into 512KB pieces for parallel downloads

---

## 2. Architecture Components

### 2.1 Three Main Modules

1. **Common Module** (`common/`)
   - Shared network protocol utilities (`proto.h/cpp`)
   - SHA1 hashing implementation (`sha1.h/cpp`)
   - Used by both tracker and client

2. **Tracker Module** (`tracker/`)
   - Central server maintaining metadata
   - User authentication and management
   - Group and file registry
   - Synchronization with other trackers

3. **Client Module** (`client/`)
   - User interface and command processing
   - Peer server (serves file pieces to others)
   - Download manager (fetches pieces from peers)
   - Tracker communication for metadata

### 2.2 Communication Patterns

- **Client ↔ Tracker**: Request/Response over TCP (metadata only)
- **Tracker ↔ Tracker**: Synchronization messages over TCP
- **Client ↔ Client**: Direct P2P file piece transfers over TCP

---

## 3. Data Structures and Storage

### 3.1 Tracker Storage

**In-Memory Structures:**
- `users`: Hash map for O(1) user lookup
- `groups`: Hash map with pair (owner, member set)
- `requests`: Hash map of vectors (pending join requests)
- `files`: Hash map with compound key "group filename"

**Persistent Storage:**
- All the above 4 in-memory structures are stored in .txt files in `tracker_data_0/` directory and `tracker_data_1/` directory (users.txt, groups.txt, requests.txt, files.txt)
- Simple formats for readability
- Saved after every state change
- Loaded on startup

### 3.2 Client Storage

**Download Tracking:**
- `downloads` map: Key = "group:filename", Value = DownloadStatus
- DownloadStatus tracks:
  - Which pieces downloaded (bitmap)
  - How many remaining
  - Completion status

**File Sharing:**
- `uploaded_files` map: filename → filepath
- Used by peer server to locate files to serve

### 3.3 Network Message Formats

**Client-Tracker Messages:**
- Text-based commands with space-separated parameters
- Length-prefixed for reliable transmission

**Peer-Peer Messages:**
- `GETPIECE filename index` (text request)
- `OK` + 4-byte size + binary data (response)

**Tracker-Tracker Messages:**
- `SYNC <command> <params...>` format
- Same format as client commands, prefixed with "SYNC"

---

## 4. Network Protocols

### 4.1 Client-Tracker Protocol

**Request Format:**
```
[4-byte length][command string]
```

**Response Format:**
```
[4-byte length][response string]
```

**Examples:**
- `REGISTER user1 pass1` → `OK` or `ERR user_exists`
- `LOGIN user1 pass1` → `OK` or `ERR wrong_password`
- `CREATE_GROUP user1 group1` → `OK` or `ERR grp_exists`
- `UPLOAD_META group file.txt 1024 2 hash peer user hash1 hash2` → `OK`

### 4.2 Peer-to-Peer Protocol

**Request (Client → Peer):**
```
[4-byte length]["GETPIECE filename index"]
```

**Response (Peer → Client):**
```
[4-byte length]["OK"]
[4-byte piece_size (network order)]
[piece_size bytes of binary data]
```

**Error Response:**
```
[4-byte length]["ERR"]
```

### 4.3 Tracker Synchronization Protocol

**Sync Message:**
```
[4-byte length]["SYNC <command> <params...>"]
```

**Acknowledgment:**
```
[4-byte length]["OK"]
```

**Important**: Sync happens asynchronously in background thread

---

## 5. Common Module (Shared Utilities)

### 5.1 Network Protocol (`proto.h/cpp`)

**Purpose**: Provides reliable message transmission over TCP sockets.

#### 5.1.1 Key Functions

**`send_all(int fd, const void *buf, size_t len)`**
- **Problem Solved**: Standard `send()` may not transmit all bytes in one call
- **Solution**: Loops until all bytes are sent
- **Implementation**:
  ```cpp
  while(remaining > 0) {
      wrote = send(fd, cursor, remaining, 0);
      if(wrote <= 0) return -1;  // Error
      cursor += wrote;
      remaining -= wrote;
  }
  ```

**`recv_all(int fd, void *buf, size_t len)`**
- Similar to `send_all` but for receiving
- Ensures all expected bytes are received

**`send_msg(int fd, const string &s)`**
- **Protocol Format**: `[4-byte length (network byte order)][message data]`
- First sends length as 32-bit integer (big-endian), then message bytes
- Uses `htonl()` to convert host byte order to network byte order

**`recv_msg(int fd, string &out)`**
- Reads 4-byte length first, then allocates buffer and receives message
- Uses `ntohl()` to convert network byte order to host byte order
- Includes safety check: messages capped at 2MB

**`split_ws(const string &s)`**
- Splits string by whitespace into token vector
- Used for parsing command-line style protocols

#### 5.1.2 Why Length-Prefixed Messages?

- **Problem**: TCP is a byte stream (no message boundaries)
- **Solution**: Send length first so receiver knows how many bytes to expect
- **Benefit**: Receiver can allocate exact buffer size

### 5.2 SHA1 Hashing (`sha1.h/cpp`)

**Purpose**: Cryptographic hash function for file integrity verification.

#### 5.2.1 Two Functions

**`sha1(data, len, out[20])`**
- Computes 20-byte binary hash digest
- Implements SHA1 algorithm from FIPS PUB 180-4
- Processes data in 512-bit (64-byte) blocks

**`sha1_hex(data, len, outhex[41])`**
- Same as `sha1()` but outputs 40-character hexadecimal string
- User-friendly format for display and storage

#### 5.2.2 SHA1 Algorithm Overview

1. **Padding**: Append `0x80`, then pad to multiple of 64 bytes
2. **Length**: Append 64-bit bit length at end
3. **Processing**: For each 64-byte block:
   - Expand 16 words → 80 words
   - Perform 80 rounds of mixing (different functions for different rounds)
   - Update 5 hash state variables (h0-h4)
4. **Output**: Combine h0-h4 into 20-byte digest

#### 5.2.3 Usage in System

- **Piece Verification**: Each 512KB piece has its SHA1 hash
- **File Verification**: Complete file hash = SHA1(concatenate all piece hashes)

---

## 6. Tracker Module

### 6.1 Responsibilities

1. **User Management**: Registration, authentication, session tracking
2. **Group Management**: Creation, membership, requests, ownership
3. **File Registry**: Metadata storage (filename, size, hashes, peer list)
4. **Synchronization**: Keep state consistent between two trackers
5. **Peer Discovery**: Help clients find peers that have specific files

### 6.2 Data Structures

#### 6.2.1 User Structure
```cpp
struct User {
    string pass;    // Password (plaintext - simplified for this system)
    bool logged;     // Whether user is currently logged in
};
```
- Storage: `unordered_map<string, User> users`
- Key: user ID, Value: User object

#### 6.2.2 File Structure
```cpp
struct File {
    string group;              // Group this file belongs to
    string filename;            // File name
    string owner;              // User who uploaded it
    string sha;                // SHA1 hash of complete file
    uint64_t size;             // File size in bytes
    vector<string> piece_sha;  // SHA1 hash for each piece
    set<string> peers;         // Set of peer addresses (IP:port) that have this file
};
```
- Storage: `unordered_map<string, File> files`
- Key: `"group filename"` (compound key), Value: File object

#### 6.2.3 Group Structure
```cpp
unordered_map<string, pair<string, set<string>>> groups;
// Key: group_id
// Value: (owner_id, set of member_ids)
```

#### 6.2.4 Request Structure
```cpp
unordered_map<string, vector<string>> requests;
// Key: group_id
// Value: List of user_ids with pending join requests
```

### 6.3 Main Functions and Control Flow

#### 6.3.1 Initialization (`main()`)

```
1. Parse command line: tracker_info.txt and tracker index (0 or 1)
2. Call load() - Restore state from disk files
3. Read tracker list from tracker_info.txt
4. Create TCP socket, bind to port, start listening
5. Enter main loop: accept() client connections
6. For each connection: spawn thread → serve_client()
```

#### 6.3.2 Client Request Handling (`serve_client()`)

**Flow:**
```
1. Loop: receive message from client
2. Split message into tokens
3. Call handle_command(parts, fd)
4. Send response back to client
5. Close connection when client disconnects
```

**Thread Model**: Each client connection handled in separate detached thread

#### 6.3.3 Command Processing (`handle_command()`)

This is the **central dispatcher** for all client commands. It:
1. Validates command format and parameters
2. Checks permissions (member check, owner check)
3. Updates data structures (protected by mutex)
4. Saves state to disk
5. Sends response to client
6. Broadcasts sync message to other tracker (for state changes).
  ("State" here means the tracker's entire application data model — the information the tracker keeps in memory (and on disk) that represents users, groups, permissions, files and requests.)

**Command Categories:**

**A. User Commands**
- `REGISTER user_id password`: Creates new user
- `LOGIN user_id password`: Authenticates user, sets logged=true

**B. Group Commands**
- `CREATE_GROUP user_id group_id`: Creates group, user becomes owner
- `JOIN_GROUP user_id group_id`: Adds join request (requires owner approval)
- `ACCEPT_REQUEST group_id user_id owner`: Owner accepts join request
- `LEAVE_GROUP user_id group_id`: User leaves, files removed if owner leaves
- `LIST_GROUPS`: Returns all group IDs

**C. File Commands**
- `UPLOAD_META group filename size npieces file_hash peer user hash1 hash2 ...`:
  - Client registers file metadata with tracker
  - Tracker stores File structure with piece hashes and peer address
- `LIST_FILES group user`: Returns filenames in group (members only)
- `GET_FILE_PEERS group filename user`: Returns file metadata + peer list
- `STOP_SHARE group filename peer`: Removes peer from file's peer list
- `ADD_PEER group filename peer`: Adds peer when download completes

#### 6.3.4 Persistence (`save()` and `load()`)

**Save Process:**
- Writes all state to text files in `tracker_data_<idx>/` directory:
  - `users.txt`: One line per user "user_id password"
  - `groups.txt`: One line per group "group_id owner member1 member2 ..."
  - `requests.txt`: One line per group with requests "group_id user1 user2 ..."
  - `files.txt`: Complex format with all file metadata

**Load Process:**
- Reads files on startup and reconstructs in-memory data structures
- Allows tracker to recover state after restart

#### 6.3.5 Synchronization (`broadcast_sync()` and `handle_sync()`)

**Why Needed**: Two trackers must stay in sync for high availability

**Sync Flow:**
```
Tracker 0 receives client command → Updates state → save() → 
broadcast_sync("SYNC <command>") → 
Tracker 1 receives sync → handle_sync() → Updates its state → save()
```

**Implementation:**
- `broadcast_sync()`: Sends sync message to other tracker in background thread
- `fire_and_forget()`: Connects to other tracker, sends message, receives ACK. broadcast_sync() uses the fire_and_forget() mechanism to achieve tracker synchronization.
- `handle_sync()`: Parses sync message and applies same state change locally

**Sync Commands:**
- All state-changing commands are synced: REGISTER, CREATE_GROUP, JOIN_GROUP, ACCEPT_REQUEST, LEAVE_GROUP, UPLOAD_META, ADD_PEER, STOP_SHARE

**Important**: SYNC messages are processed by `handle_sync()` which calls `save()` after updating state

### 6.4 Thread Safety

- **Single Mutex**: `mutex mtx` protects all shared data structures
- **Lock Scope**: Lock acquired at start of `handle_command()` for state-changing operations
- **Read Operations**: Also locked to prevent race conditions during reads

---

## 7. Client Module

### 7.1 Responsibilities

1. **User Interface**: Command-line interface for user interaction
2. **Tracker Communication**: Query tracker for metadata and peer lists
3. **File Management**: Upload files, compute hashes, serve pieces
4. **Download Management**: Download pieces from peers in parallel
5. **Peer Server**: Serve file pieces to other peers requesting them

### 7.2 Data Structures

#### 7.2.1 Download Status
```cpp
struct DownloadStatus {
    string group, filename, dest; // dest refers to the destination file path on the 
                                  // downloader's own computer
    int npieces;
    vector<int> have;          // Bitmap: 1 if piece downloaded, 0 if not
    atomic<int> remaining; // atomic<int> is an integer that is guaranteed to be 
                           // safe from race conditions when accessed by multiple threads.
    atomic<bool> completed, running;
    mutex m;                   // Protects 'have' vector
};
```

**Purpose**: Track progress of ongoing downloads
- `have[i] = 1` means piece `i` is downloaded
- `remaining` counts pieces still needed
- Multiple threads update this structure concurrently. Safety from race conditions is ensured by using atomic<>.
- A variable of this structure type is defined for every file being downloaded.
 and whenever a file's download starts an entry containing the link to this variable is put in the global state "Downloads" map.

#### 7.2.2 State of client 

(Note: A client's state in any session is not required to be stored for next sessions, so we don't store the client state datastructures in any file. So client state is not persistent, all the state datastructures, variables are stored in-memory only.)

Definition of Client state parameters: Every client instance maintains the following datastructures/variables in order to carry out its tasks. These parameters are separate, independent for each client.

- `vector<string> trackers`: List of all tracker addresses
- `string connected_tracker`: Currently active tracker
- `string current_user`: Logged-in user ID
- `map<string, string> uploaded_files`: filename → filepath for shared files
- `int peer_port`: Port our peer server listens on
- `map<string, shared_ptr<DownloadStatus>> downloads`: Active downloads                                                     
 
 -[Explanation of global state 'downloads' map:                                                       
"global map" is a data structure that exists within the client program. It's a central list that keeps track of every file the user is currently trying to download.

  Key (string): The filename of the file being downloaded (e.g., "report.pdf").
Value (shared_ptr<DownloadStatus>): A smart pointer to the DownloadStatus object that holds all the progress information for that specific file.
  The "Register" Action
  "Registering the download" simply means adding a new entry to this map.

  Here is the sequence of events:

  A user types download_file my_group report.pdf ...
  The client creates a new DownloadStatus object specifically for report.pdf. This object contains the have vector, the destination path, etc.
  The client then performs the "register" step: it adds this new object to the downloads map using the filename as the key.
  downloads["report.pdf"] = new_download_status_object;
  Why is this important?
  This map acts as the client's master control panel for all ongoing downloads.

  (i) Managing Multiple Downloads: It allows the client to handle several downloads at the same time. You could be downloading file1.zip and file2.iso simultaneously, and the downloads map would have two separate entries, each tracking its own progress independently.

  (ii) Centralized Access: Any part of the client program that needs to know the status of a download (e.g., the threads fetching the pieces, or a potential show downloads command) can easily look it up in this central map using the filename.

  (iii) Lifecycle Management: When a download is complete or cancelled, its entry is removed from the map. This signals that the download is no longer active and allows the DownloadStatus object to be cleaned up from memory.]
                                                      
NOTE: All these above global state items are stored in the clients only, not trackers.

### 7.3 Main Components

#### 7.3.1 Initialization (`main()`)

**Flow:**
```
1. Parse command line: tracker IP:port and tracker_info.txt
2. Read tracker list from tracker_info.txt
3. Set connected_tracker from command line
4. Call start_peer_server() - Start peer server thread
5. Enter command loop: read user commands, parse, execute
```

#### 7.3.2 Peer Server (`peer_server_thread()`)

**Purpose**: Serve file pieces to other peers who are downloading

**Flow:**
```
Note: For every peer connection request, the client creates a new, separate TCP socket. This is 
      different from the socket used in the main() function to connect with the tracker. 

1. Create TCP socket, bind to port, listen(). (listen() means to wait for a new incoming
                                               peer connection request)
2. Loop: accept() connections from peers
3. For each connection (detached thread):
   a. Receive request: "GETPIECE filename piece_index"
   b. Parse request, validate
   c. Look up file path in uploaded_files map
   d. Open file, seek to piece position
   e. Read piece data (512KB or less for last piece)
   f. Send "OK", then piece size (4 bytes), then piece data
   g. Close connection (i.e. after sending each piece, the connection is closed. For the 
                             next piece request from this client, a new TCP socket will be 
                             created)
```
```
Note (Small Detour): 

Each client has 4 kinds of sockets/connections:
1) for Client-Tracker connection: The TCP connection between the client and the tracker is 
                                  persistent. It is created once and stays active for the entire 
                                  program session. It is closed only when the program ends or the 
                                  tracker gets terminated. Each client has 1 of this.

3) for listening to incoming connection requests from other peer clients: This stays active for the 
                                        entire program session. Each client has 1 of this.

3) for sending the requested single file-piece to the peer client: The TCP connection between 2 peer 
                                        clients is created only for transferring 1 piece of a file. 
                                        Once the transfer is done, this temporary socket is closed.
                                        Each client can several connections/sockets of this type at 
                                        a time.

4) for requesting 1 file piece from a peer client: This TCP connection stays active only till the 
                                        currently required file piece is getting downloaded. After 
                                        the download, it is closed. Each client can several 
                                        connections/sockets of this type at a time.

```
![alt text](image-1.png)

(Resuming after the small detour) 

**Key Details of peer_server_thread():**
- Each request handled in separate thread (allows concurrent serving)
- Pieces read directly from file using `fseek()` and `fread()`
- Piece size sent in network byte order (4-byte integer)

#### 7.3.3 File Upload (`upload_file` command)

**Flow:**
```
1. User command: upload_file <group> <filepath>
2. Call compute_piece_and_file_sha1():
   a. Read file, split into 512KB pieces
   b. Compute SHA1 hash for each piece
   c. Concatenate piece hashes, compute SHA1 of concatenation (file hash)
      (explanation: The SHA1 of the overall file is calculated by concatenating the SHA1 of the individual  
                    pieces and then calculating the SHA1 of that concatenated string.)
3. Extract filename from path
4. Add file to uploaded_files map
5. Send UPLOAD_META to tracker with all hashes and peer address (peer address of the uploader is 
       sent to the tracker so that the tracker knows which client has this particular file. 
       If in the future, any client asks for this file, then the tracker will give him 
       the addresss of this uploader client to fetch the file pieces.)
6. Tracker stores metadata and adds peer to file's peer list
```

**Important**: File data is NOT sent to tracker, only metadata (hashes, size, peer address)

#### 7.3.4 File Download (`download_file` command)

This is the **most complex operation**. Let's break it down:

**Step 1: Get Metadata from Tracker**
```
1. Send GET_FILE_PEERS <group> <filename> <user> to tracker
2. Receive response with:
   - File size
   - Number of pieces
   - File hash
   - Piece hashes (comma-separated)
   - List of peer addresses
```

**Step 2: Prepare Download**
```
1. Create output file and pre-allocate size (ftruncate). [Pre-allocate size is done byusing the 
                                                          file-size given by the tracker in Step 1]
2. Create DownloadStatus structure [This datastructure will be used to keep track which piece has 
                                    been downloaded and which piece is pending]
3. Initialize 'have' vector to all zeros [i.e. Initially, none of the pieces has been downloaded]
4. Register download in global downloads map
```

**Step 3: Parallel Download** (`run_download_job()`)
```
1. Download pieces in batches (MAX_SIM_PIECES = 8 at a time)
2. For each piece in batch (parallel threads):
   a. Try each peer until one succeeds
   b. For each peer, retry up to 2 times
   c. Call fetch_one_piece() to download
3. Wait for batch to complete before starting next batch
```

**Step 4: Piece Download** (`fetch_one_piece()`)
```
1. Connect to peer (TCP socket)
2. Send request: "GETPIECE filename index"
3. Receive "OK" response
4. Receive piece size (4 bytes, network order)
5. Receive piece data
6. Verify SHA1 hash matches expected
7. Write piece to correct position in destination file (lseek + write)
8. Return success/failure
```

**Step 5: Completion**
```
1. When all pieces downloaded (remaining == 0):
   a. Verify complete file hash matches expected
   b. If valid: Register ourselves as peer with tracker (ADD_PEER)
   c. Add file to uploaded_files so we can serve it
```

**Background Downloads**: If command ends with `&`, download runs in detached thread, allowing user to continue with other commands

#### 7.3.5 Tracker Communication (`tracker_roundtrip()`)

**Purpose**: Send message to tracker with automatic failover

**Flow:**
```
1. Try connected_tracker first
2. If fails, try all other trackers in list
3. If one succeeds, update connected_tracker to that one
4. Return success/failure
```

**Benefit**: Client automatically switches to backup tracker if primary fails

### 7.4 File Hashing (`compute_piece_and_file_sha1()`)

**Algorithm:**
```
1. Open file, get size
2. Calculate number of pieces: (size + PIECE_SZ - 1) / PIECE_SZ
3. For each piece:
   a. Read piece data (512KB or less for last piece)
   b. Compute SHA1 hash → store in piece_hex vector
4. Concatenate all piece hashes into one string
5. Compute SHA1 of concatenation → file_hex
6. Return piece hashes, file hash, and size
```

**Why Two-Level Hashing?**
- **Piece Hash**: Verify each piece individually (catch corruption early)
- **File Hash**: Verify complete file integrity after assembly

### 7.5 Thread Safety

**Mutexes:**
- `uploaded_mtx`: Protects `uploaded_files` map
- `downloads_mtx`: Protects `downloads` map
- `DownloadStatus::m`: Protects individual download's `have` vector

**Atomic Variables:**
- `DownloadStatus::remaining`: Thread-safe counter (no mutex needed)
- `DownloadStatus::completed`, `running`: Thread-safe flags

---

## 8. Control Flow Between Components

### 8.1 Complete User Registration Flow

```
┌─────────┐                    ┌──────────┐                    ┌──────────┐
│ Client  │                    │ Tracker0 │                    │ Tracker1 │
└────┬────┘                    └────┬─────┘                    └─────┬────┘
     │                              │                                │
     │ REGISTER user1 pass1         │                                │
     ├─────────────────────────────►│                                │
     │                              │                                │
     │                              │ [Lock mutex]                   │
     │                              │ users[user1] = User(pass1)     │
     │                              │ save()                         │
     │                              │ [Unlock mutex]                 │
     │                              │                                │
     │                              │ broadcast_sync("REGISTER...")  │
     │                              │         │                      │
     │                              │         │ SYNC REGISTER...     │
     │                              │         ├─────────────────────►│
     │                              │         │                      │
     │                              │         │                handle_sync()
     │                              │         │                users[user1] = ...
     │                              │         │                save()
     │                              │         │                      │
     │                              │         ◄─────── OK ───────────┤
     │                              │                                │
     │ OK                           │                                │
     │◄─────────────────────────────┤                                │
     │                              │                                │
```

### 8.2 File Upload Flow

```
┌─────────┐                    ┌──────────┐                    ┌──────────┐
│ Client  │                    │ Tracker0 │                    │ Tracker1 │
└────┬────┘                    └────┬─────┘                    └────┬─────┘
     │                              │                               │
     │ upload_file group1 file.txt  │                               │
     │                              │                               │
     │ [Local Processing]           │                               │
     │ compute_piece_and_file_sha1()│                               │
     │ - Split file into pieces     │                               │
     │ - Hash each piece            │                               │
     │ - Hash concatenated hashes   │                               │
     │ uploaded_files[file.txt] = path│                             │
     │                              │                               │
     │ UPLOAD_META group1 file.txt  │                               │
     │ size npieces file_hash peer  │                               │
     │ user hash1 hash2 ...         │                               │
     ├─────────────────────────────►│                               │
     │                              │                               │
     │                              │ [Lock mutex]                  │
     │                              │ Check user is group member    │
     │                              │ files["group1 file.txt"] =    │
     │                              │   File{group, filename, ...}  │
     │                              │ save()                        │
     │                              │ [Unlock mutex]                │
     │                              │                               │
     │                              │ broadcast_sync("UPLOAD_META...")│
     │                              │         │                     │
     │                              │         │ SYNC UPLOAD_META... │
     │                              │         ├────────────────────►│
     │                              │         │                     │
     │                              │         │                handle_sync()
     │                              │         │                files["..."] = ...
     │                              │         │                save()
     │                              │         │                     │
     │                              │         ◄─────── OK ──────────┤
     │                              │                               │
     │ OK                           │                               │
     │◄─────────────────────────────┤                               │
     │                              │                               │
```

### 8.3 File Download Flow

```
┌─────────┐     ┌──────────┐       ┌─────────┐     ┌──────────┐
│Client A │     │ Tracker  │       │Client B │     │Client C  │
└────┬────┘     └────┬─────┘       └────┬────┘     └────┬─────┘
     │               │                  │               │
     │ GET_FILE_PEERS group1 file.txt   │               │
     ├──────────────►│                  │               │
     │               │                  │               │
     │               │ [Lock mutex]     │               │
     │               │ Get file metadata│               │
     │               │ Return:          │               │
     │               │ - size, npieces  │               │
     │               │ - file_hash      │               │
     │               │ - piece_hashes   │               │
     │               │ - peer list      │               │
     │               │   (B, C)         │               │
     │               │ [Unlock mutex]   │               │
     │               │                  │               │
     │ Response with peer list          │               │
     │◄──────────────┤                  │               │
     │               │                  │               │
     │ [Start parallel download]        │               │
     │                                  │               │
     │ GETPIECE file.txt 0              │               │
     ├─────────────────────────────────►│               │
     │               │                  │               │
     │               │ [Read piece 0    │               │
     │               │  from file]      │               │
     │               │                  │               │
     │ OK + size + data                 │               │
     │◄─────────────────────────────────┤               │
     │                                  │               │
     │ GETPIECE file.txt 1              │               │
     ├─────────────────────────────────────────────────►│
     │                                  │               │
     │                                  │ [Read piece 1]│
     │                                  │               │
     │ OK + size + data                 │               │
     │◄─────────────────────────────────────────────────┤
     │                                  │               │
     │ [Verify hashes, write to file]   │               │
     │                                  │               │
     │ [When complete]                  │               │
     │                                  │               │
     │ ADD_PEER group1 file.txt peerA   │               │
     ├──────────────►│                  │               │
     │               │ Update peer list │               │
     │               │ sync to Tracker1 │               │
     │               │                  │               │
     │ OK            │                  │               │
     │◄──────────────┤                  │               │
     │               │                  │               │
```

### 8.4 Tracker Synchronization Flow

```
┌──────────┐                              ┌──────────┐
│ Tracker0 │                              │ Tracker1 │
└────┬─────┘                              └────┬─────┘
     │                                         │
     │ [Client sends CREATE_GROUP]             │
     │ [Tracker0 processes]                    │
     │   - Updates groups map                  │
     │   - Calls save()                        │
     │                                         │
     │ broadcast_sync("CREATE_GROUP ...")      │
     │   │                                     │
     │   │ [Background thread]                 │
     │   │                                     │
     │   │ fire_and_forget("SYNC CREATE_GROUP...")│
     │   │                                     │
     │   │  [TCP Connect]                      │
     │   ├────────────────────────────────────►│
     │   │                                     │
     │   │                              [In serve_client thread]
     │   │                                     │
     │   │                              handle_command()
     │   │                                (cmd == "SYNC")
     │   │                                     │
     │   │                              handle_sync("CREATE_GROUP...")
     │   │                                - Parse command
     │   │                                - Update groups map
     │   │                                - save()
     │   │                                     │
     │   │ OK                                  │
     │   │◄────────────────────────────────────┤
     │   │                                     │
     │   │ [Both trackers now in sync]         │
     │   │                                     │
```

---

## 9. Feature Implementation Details

### 9.1 User Authentication

**Registration:**
- Client sends: `REGISTER user_id password`
- Tracker checks if user exists → if not, creates User object
- Password stored in plaintext (simplified for this system)
- State saved to disk, synced to other tracker

**Login:**
- Client sends: `LOGIN user_id password`
- Tracker validates password → sets `logged = true`
- Client stores `current_user` for session

**Logout:**
- Client clears `current_user`
- Clears `uploaded_files` (stops sharing all files)
- Tracker's `logged` flag could be reset (not explicitly in code)

### 9.2 Group Management

**Group Creation:**
- Creator becomes owner automatically
- Creator added to members set
- Owner has special privileges (accept requests, etc.)

**Join Request:**
- User sends `JOIN_GROUP` → request added to `requests[group]` vector
- Owner uses `list_requests` to see pending requests
- Owner uses `accept_request` to approve → user moved to members set

**Leave Group:**
- User removed from members set
- If user was owner:
  - If group empty → delete group
  - Otherwise → transfer ownership to first remaining member
- All files owned by leaving user are deleted from group

### 9.3 File Sharing (Upload)

**Process:**
1. File read and split into 512KB pieces
2. Each piece hashed individually
3. All piece hashes concatenated, then hashed (file hash)
4. Metadata sent to tracker:
   - Group, filename, size, number of pieces
   - File hash, all piece hashes
   - Peer address (so others can download from this client)
5. Tracker stores metadata, adds peer to file's peer list
6. File added to `uploaded_files` map (so peer server can serve it)

**Key Point**: Actual file data never goes to tracker, only metadata

### 9.4 File Downloading

**Multi-Peer Strategy:**
- Tracker provides list of peers that have the file
- Client tries multiple peers for each piece (for reliability)
- Up to 8 pieces downloaded simultaneously from different peers

**Integrity Verification:**
- Each piece verified immediately after download (compare SHA1)
- If hash mismatch → discard piece, try another peer
- After all pieces downloaded → verify complete file hash

**Batch Processing:**
- Pieces downloaded in batches of MAX_SIM_PIECES (8)
- Wait for batch to complete before starting next batch
- Prevents overwhelming system with too many threads

**Completion:**
- After successful download and verification:
  - Client registers itself as peer (ADD_PEER to tracker)
  - Adds file to `uploaded_files` (can now serve to others)
  - Download status marked as completed

### 9.5 Multi-Tracker Failover

**Mechanism:**
- Client maintains list of all trackers
- On each request, tries `connected_tracker` first
- If fails, iterates through other trackers
- If one responds, updates `connected_tracker` to that one

**Why Two Trackers?**
- High availability: if one fails, system continues
- Load distribution: requests can go to either tracker
- Data consistency: both maintain same state via sync

**Synchronization:**
- Every state change synced immediately
- Sync happens in background thread (doesn't block client)
- Other tracker applies same state change locally

### 9.6 Piece-Based Transfer

**Why Pieces?**
- Allows parallel downloads from multiple peers
- Enables partial file availability (can serve pieces we have)
- Faster downloads (multiple simultaneous connections)
- Easier error recovery (re-download only failed pieces)

**Piece Size:**
- Fixed at 512KB (524288 bytes)
- Last piece may be smaller
- Balance between overhead and granularity

---

## 14. Threading and Concurrency

### 14.1 Tracker Threading

**Main Thread:**
- Accepts client connections
- Spawns worker thread for each connection

**Worker Threads:**
- One per client connection
- Handles all commands from that client
- Accesses shared data through mutex lock

**Sync Thread:**
- Background thread for broadcasting sync messages
- Doesn't block client responses

**Mutex Protection:**
- Single `mtx` mutex protects all shared structures
- Lock acquired for both reads and writes
- Ensures no race conditions

### 14.2 Client Threading

**Main Thread:**
- Command loop (user interface)
- Processes commands synchronously (except background downloads)

**Peer Server Thread:**
- Accepts connections from peers
- Spawns worker thread for each peer request

**Download Threads:**
- Multiple threads for parallel piece downloads (max 8 at a time)
- Each thread downloads one piece
- Updates shared DownloadStatus structure (protected by mutex)

**Background Download Thread:**
- If download command ends with `&`, runs in detached thread
- User can continue with other commands while downloading

**Thread Safety Mechanisms:**
- Mutexes for shared data (`uploaded_mtx`, `downloads_mtx`, `DownloadStatus::m`)
- Atomic variables for counters (`DownloadStatus::remaining`, `DownloadStatus::completed`, `DownloadStatus::running`)

---

## 15. Error Handling and Recovery

### 15.1 Network Errors

**Connection Failures:**
- Timeout settings: 10 seconds for tracker, 15 seconds for peers
- Automatic retry (up to 2 times per peer for piece downloads)
- Failover to backup tracker if primary fails

**Partial Transmissions:**
- `send_all()` and `recv_all()` handle partial send/recv automatically
- Loop until all bytes transmitted/received

### 15.2 File Errors

**Hash Mismatch:**
- Piece hash verification fails → discard piece, try another peer
- File hash verification fails → download considered failed
- Integrity guaranteed before marking download complete

**File I/O Errors:**
- File read errors during upload → return error to user
- File write errors during download → return failure, piece retried

### 15.3 State Recovery

**Tracker Recovery:**
- State saved to disk after every change
- On restart, `load()` reconstructs all data structures
- Ensures persistence across crashes

**Client Recovery:**
- Download state maintained in memory
- On client restart, downloads lost (would need to re-download)
- Shared files need to be re-registered with tracker

### 15.4 Synchronization Failures

**If Sync Fails:**
- Warning message logged
- Local state still updated (eventual consistency)
- Manual recovery possible (restart both trackers from saved state)

---

## Summary of Key Concepts

### System Architecture
- **Centralized Metadata** (trackers) + **Decentralized Transfer** (P2P)
- Two synchronized trackers for high availability
- Clients act as both downloaders and uploaders (peers)

### Data Flow
1. **Upload**: Client → Tracker (metadata) → Other clients download directly
2. **Download**: Client → Tracker (peer list) → Client → Peers (file pieces)
3. **Sync**: Tracker → Tracker (state changes)

### Key Algorithms
1. **File Hashing**: Two-level (piece hash + file hash) for integrity
2. **Parallel Downloads**: Batched multi-threaded piece fetching
3. **Failover**: Automatic tracker switching on failure
4. **Synchronization**: Real-time state replication between trackers

### Threading Model
- **Tracker**: One thread per client connection
- **Client**: Peer server thread + download worker threads
- Mutex protection for all shared data structures

### Protocol Design
- Length-prefixed messages for reliable transmission
- Text-based commands for human readability
- Binary data only for actual file pieces

---

## 10. Complete Function Reference

### 10.1 Common Module Functions

#### proto.cpp Functions

**`ssize_t send_all(int fd, const void *buf, size_t len)`**
- **File**: `common/proto.cpp`
- **Purpose**: Reliably send all bytes even if `send()` returns partial data
- **How it works**: Loops calling `send()` until all `len` bytes are transmitted
- **Used by**: `send_msg()`, client peer communication, tracker sync
- **Returns**: Number of bytes sent (should equal `len`) or -1 on error

**`ssize_t recv_all(int fd, void *buf, size_t len)`**
- **File**: `common/proto.cpp`
- **Purpose**: Reliably receive all bytes even if `recv()` returns partial data
- **How it works**: Loops calling `recv()` until all `len` bytes are received
- **Used by**: `recv_msg()`, all network communication
- **Returns**: Number of bytes received (should equal `len`) or -1 on error

**`bool send_msg(int fd, const std::string &s)`**
- **File**: `common/proto.cpp`
- **Purpose**: Send a length-prefixed string message over TCP
- **How it works**: 
  1. Converts string length to network byte order (htonl)
  2. Sends 4-byte length prefix via `send_all()`
  3. Sends message data via `send_all()`
- **Used by**: All client-tracker communication, tracker synchronization, peer requests
- **Dependencies**: `send_all()`, `htonl()`
- **Returns**: true on success, false on error

**`bool recv_msg(int fd, std::string &out)`**
- **File**: `common/proto.cpp`
- **Purpose**: Receive a length-prefixed string message from TCP
- **How it works**:
  1. Receives 4-byte length prefix via `recv_all()`
  2. Converts from network byte order (ntohl)
  3. Resizes output string and receives message data via `recv_all()`
- **Used by**: All network communication (client, tracker, peer-to-peer)
- **Dependencies**: `recv_all()`, `ntohl()`
- **Safety**: Caps messages at 2MB to prevent huge allocations
- **Returns**: true on success, false on error

**`std::vector<std::string> split_ws(const std::string &s)`**
- **File**: `common/proto.cpp`
- **Purpose**: Split string into whitespace-separated tokens
- **How it works**: Uses `istringstream` to extract tokens separated by any whitespace
- **Used by**: Command parsing in both client and tracker
- **Returns**: Vector of token strings

#### sha1.cpp Functions

**`void sha1(const uint8_t *data, size_t len, uint8_t out[20])`**
- **File**: `common/sha1.cpp`
- **Purpose**: Compute SHA1 cryptographic hash (binary output)
- **How it works**:
  1. Pads input to multiple of 64 bytes (512 bits)
  2. Processes each 64-byte block through 80 rounds of mixing
  3. Combines results into 20-byte digest
- **Used by**: `sha1_hex()` only (not called directly elsewhere)
- **Dependencies**: `rotl()` (rotate left helper function)
- **Output**: 20-byte binary hash in `out` array

**`void sha1_hex(const uint8_t *data, size_t len, char outhex[41])`**
- **File**: `common/sha1.cpp`
- **Purpose**: Compute SHA1 hash and format as hexadecimal string
- **How it works**:
  1. Calls `sha1()` to get binary hash
  2. Converts each byte to 2 hex characters
  3. Null-terminates the string
- **Used by**: 
  - `compute_piece_and_file_sha1()` (client upload)
  - `fetch_one_piece()` (piece verification)
  - `run_download_job()` (file verification)
- **Dependencies**: `sha1()`
- **Output**: 40-character hex string + null terminator

---

### 10.2 Tracker Module Functions

#### tracker.cpp Functions

**`bool is_member(const string& user, const string& group)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Check if user is a member of a group
- **How it works**: Looks up group in `groups` map, checks if user is in members set
- **Used by**: `handle_command()` for permission checking
- **Returns**: true if user is member, false otherwise

**`bool is_owner(const string& user, const string& group)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Check if user is the owner of a group
- **How it works**: Looks up group in `groups` map, compares user with owner field
- **Used by**: `handle_command()` for owner-only operations
- **Returns**: true if user is owner, false otherwise

**`void save()`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Persist all state to disk files
- **How it works**: 
  1. Creates `tracker_data_<idx>/` directory
  2. Writes users to `users.txt`
  3. Writes groups to `groups.txt`
  4. Writes requests to `requests.txt`
  5. Writes files to `files.txt`
- **Used by**: `handle_command()` after state changes, `handle_sync()` after sync operations
- **Called from**: Every state-modifying operation
- **Dependencies**: File I/O operations, data structure iteration

**`void load()`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Restore state from disk files on startup
- **How it works**:
  1. Opens all data files
  2. Parses each line and reconstructs data structures
  3. Populates `users`, `groups`, `requests`, `files` maps
- **Used by**: `main()` at tracker startup
- **Dependencies**: File I/O, string parsing
- **Called once**: At program initialization

**`bool fire_and_forget(const string& ep, const string& msg)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Send sync message to another tracker without waiting for full processing
- **How it works**:
  1. Creates TCP connection to other tracker
  2. Sends message via `send_msg()`
  3. Receives ACK via `recv_msg()` (but doesn't wait for processing)
  4. Closes connection
- **Used by**: `broadcast_sync()` to send sync messages
- **Dependencies**: `send_msg()`, `recv_msg()` from proto.cpp
- **Returns**: true if message sent successfully

**`void broadcast_sync(const string& cmd)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Broadcast state change to all other trackers asynchronously
- **How it works**:
  1. Spawns background thread
  2. Iterates through all trackers (skips self)
  3. For each tracker, calls `fire_and_forget()` with "SYNC " + cmd
- **Used by**: `handle_command()` after every state-changing operation
- **Dependencies**: `fire_and_forget()`, threading
- **Called from**: REGISTER, CREATE_GROUP, JOIN_GROUP, ACCEPT_REQUEST, LEAVE_GROUP, UPLOAD_META, ADD_PEER, STOP_SHARE handlers

**`void handle_sync(const string& sync_data)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Process synchronization message from another tracker
- **How it works**:
  1. Parses sync command type
  2. Applies same state change locally (mirrors the original operation)
  3. Calls `save()` to persist the synced state
- **Used by**: `handle_command()` when command is "SYNC"
- **Dependencies**: All state modification functions, `save()`
- **Called from**: Tracker-to-tracker synchronization messages

**`void handle_command(const vector<string>& parts, int fd)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Main command dispatcher - processes all client commands
- **How it works**:
  1. Extracts command type from first token
  2. Validates command format and parameters
  3. Locks mutex for thread safety
  4. Checks permissions (member/owner checks)
  5. Updates appropriate data structure
  6. Calls `save()` for persistence
  7. Sends response via `send_msg()`
  8. Calls `broadcast_sync()` for state changes
- **Used by**: `serve_client()` for every client request
- **Dependencies**: All data structures, `is_member()`, `is_owner()`, `save()`, `broadcast_sync()`, `send_msg()`
- **Handles**: All commands (REGISTER, LOGIN, CREATE_GROUP, JOIN_GROUP, LIST_GROUPS, LIST_REQUESTS, ACCEPT_REQUEST, LEAVE_GROUP, LIST_FILES, GET_FILE_PEERS, UPLOAD_META, STOP_SHARE, ADD_PEER, SYNC)

**`void serve_client(int fd)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Handle connection from a single client in dedicated thread
- **How it works**:
  1. Loops receiving messages via `recv_msg()`
  2. Splits message into tokens via `split_ws()`
  3. Calls `handle_command()` to process
  4. Continues until client disconnects or sends empty message
  5. Closes connection
- **Used by**: `main()` spawns one thread per client connection
- **Dependencies**: `recv_msg()`, `split_ws()`, `handle_command()`
- **Thread model**: One thread per client connection

**`int main(int argc, char **argv)`**
- **File**: `tracker/tracker.cpp`
- **Purpose**: Tracker entry point - initialization and main server loop
- **How it works**:
  1. Parses command line arguments (tracker_info.txt, index)
  2. Calls `load()` to restore state
  3. Reads tracker list from file
  4. Creates socket, binds to port, starts listening
  5. Spawns console handler thread for commands (save, status, quit)
  6. Main loop: accepts client connections, spawns `serve_client()` thread for each
- **Dependencies**: `load()`, `serve_client()`, socket operations
- **Initialization**: Sets up entire tracker infrastructure

---

### 10.3 Client Module Functions

#### client.cpp Functions

**`bool send_to_endpoint(const string& addr, const string& msg, string& reply)`**
- **File**: `client/client.cpp`
- **Purpose**: Create TCP connection, send message, receive reply, close connection
- **How it works**:
  1. Parses address (IP:port format)
  2. Creates TCP socket
  3. Sets timeouts (10 seconds)
  4. Connects to address
  5. Sends message via `send_msg()`
  6. Receives reply via `recv_msg()`
  7. Closes connection
- **Used by**: `tracker_roundtrip()` for all tracker communication
- **Dependencies**: `send_msg()`, `recv_msg()` from proto.cpp, socket operations
- **Returns**: true on success, false on error

**`bool tracker_roundtrip(const string& msg, string& reply)`**
- **File**: `client/client.cpp`
- **Purpose**: Send message to tracker with automatic failover to backup tracker
- **How it works**:
  1. Tries `connected_tracker` first via `send_to_endpoint()`
  2. If fails, iterates through all trackers in list
  3. If another tracker responds, updates `connected_tracker`
  4. Returns success if any tracker responded
- **Used by**: All tracker communication commands (login, create_group, upload_file, etc.)
- **Dependencies**: `send_to_endpoint()`
- **Returns**: true if any tracker responded, false if all failed
- **Key feature**: Automatic failover without user intervention

**`void compute_piece_and_file_sha1(const string& path, vector<string>& piece_hex, string& file_hex, uint64_t& size)`**
- **File**: `client/client.cpp`
- **Purpose**: Compute SHA1 hashes for file pieces and complete file
- **How it works**:
  1. Opens file, calculates size and number of pieces
  2. For each piece:
     a. Reads piece data (512KB or less for last piece)
     b. Calls `sha1_hex()` to compute hash
     c. Stores hash in `piece_hex` vector
  3. Concatenates all piece hashes
  4. Calls `sha1_hex()` on concatenation to get file hash
- **Used by**: `upload_file` command handler
- **Dependencies**: `sha1_hex()` from sha1.cpp, file I/O
- **Output**: Fills `piece_hex`, `file_hex`, and `size` parameters
- **Key purpose**: Generate integrity hashes for upload

**`void peer_server_thread(int port)`**
- **File**: `client/client.cpp`
- **Purpose**: Server thread that accepts connections and serves file pieces to peers
- **How it works**:
  1. Creates socket, binds to port, starts listening
  2. Main loop: accepts connections
  3. For each connection, spawns detached thread that:
     a. Receives "GETPIECE filename index" request via `recv_msg()`
     b. Parses request
     c. Looks up file path in `uploaded_files` map (thread-safe with mutex)
     d. Opens file, seeks to piece position, reads piece data
     e. Sends "OK" via `send_msg()`
     f. Sends piece size (4 bytes) via `send_all()`
     g. Sends piece data via `send_all()`
     h. Closes connection
- **Used by**: `start_peer_server()` spawns this as background thread
- **Dependencies**: `recv_msg()`, `send_msg()`, `send_all()` from proto.cpp, file I/O
- **Thread model**: One accepting thread, one worker thread per peer request
- **Runs**: Continuously until client exits

**`bool fetch_one_piece(const string& peer, const string& fname, int idx, const string& dest, const string& expected_sha)`**
- **File**: `client/client.cpp`
- **Purpose**: Download a single piece from a peer and verify integrity
- **How it works**:
  1. Connects to peer address via TCP
  2. Sends "GETPIECE filename index" request via `send_msg()`
  3. Receives "OK" response via `recv_msg()`
  4. Receives piece size (4 bytes, network order) via `recv_all()`
  5. Receives piece data via `recv_all()`
  6. Verifies hash: calls `sha1_hex()` on received data, compares with `expected_sha`
  7. If hash matches, opens destination file, seeks to piece position, writes data
  8. Returns success/failure
- **Used by**: `run_download_job()` for each piece download thread
- **Dependencies**: `send_msg()`, `recv_msg()`, `recv_all()` from proto.cpp, `sha1_hex()` from sha1.cpp, file I/O
- **Returns**: true if piece downloaded and verified successfully
- **Key feature**: Integrity verification before writing to disk

**`void run_download_job(string g, string fname, string dest, vector<string> hashes, vector<string> peers, uint64_t fsz, string fsha)`**
- **File**: `client/client.cpp`
- **Purpose**: Coordinate downloading complete file by fetching pieces in parallel
- **How it works**:
  1. Creates `DownloadStatus` structure
  2. Initializes download tracking (all pieces needed)
  3. Registers download in global `downloads` map
  4. Downloads pieces in batches (MAX_SIM_PIECES = 8 at a time):
     a. For each piece in batch, spawns thread that:
        - Tries each peer in list
        - For each peer, retries up to 2 times
        - Calls `fetch_one_piece()` to download
        - If successful, marks piece as downloaded (updates `have` vector, decrements `remaining`)
     b. Waits for all threads in batch to complete
  5. When all pieces downloaded:
     a. Verifies complete file: calls `compute_piece_and_file_sha1()` on destination
     b. Compares file hash and size with expected values
     c. If valid, sends ADD_PEER to tracker via `tracker_roundtrip()`
     d. Adds file to `uploaded_files` map
- **Used by**: `download_file` command handler (runs in thread if background)
- **Dependencies**: `fetch_one_piece()`, `compute_piece_and_file_sha1()`, `tracker_roundtrip()`, threading
- **Key features**: Parallel downloads, retry logic, integrity verification, auto-registration as peer

**`void print_downloads()`**
- **File**: `client/client.cpp`
- **Purpose**: Display status of all active downloads
- **How it works**:
  1. Locks `downloads_mtx` mutex
  2. Iterates through `downloads` map
  3. For each download:
     a. Counts pieces downloaded (from `have` vector)
     b. Prints status:
        - `[C]` if completed
        - `[D]` if running (with progress: pieces_have/total)
        - `[P]` if partially downloaded but not running
- **Used by**: `show_downloads` command handler
- **Dependencies**: `downloads` map access (protected by mutex)

**`vector<string> parse_hashes(const string& line)`**
- **File**: `client/client.cpp`
- **Purpose**: Extract 40-character hex hash strings from a line
- **How it works**:
  1. Scans line for hexadecimal digits
  2. When finds 40 consecutive hex digits, extracts as hash
  3. Continues until line ends
  4. Handles comma separators between hashes
- **Used by**: `download_file` command handler to parse piece hashes from tracker response
- **Returns**: Vector of hash strings
- **Purpose**: Parse comma-separated hash list from tracker

**`bool parse_download_cmd(const string& line, string& group, string& filename, string& dest)`**
- **File**: `client/client.cpp`
- **Purpose**: Parse download_file command and extract parameters
- **How it works**:
  1. Removes background marker (`&`) if present
  2. Splits command into tokens
  3. Validates format: "download_file group filename dest"
  4. Extracts and stores parameters
- **Used by**: `download_file` command handler
- **Returns**: true if parsing successful
- **Purpose**: Separate command parsing from execution

**`int main(int argc, char **argv)`**
- **File**: `client/client.cpp`
- **Purpose**: Client entry point - initialization and command loop
- **How it works**:
  1. Parses command line (tracker address, tracker_info.txt)
  2. Sets `connected_tracker`
  3. Reads tracker list from file
  4. Calls `start_peer_server()` to start peer server
  5. Main command loop:
     a. Reads user input
     b. Splits into tokens via `split_ws()`
     c. Dispatches to appropriate handler
     d. For tracker commands: uses `tracker_roundtrip()`
     e. For upload: uses `compute_piece_and_file_sha1()`
     f. For download: uses `run_download_job()` (in thread if background)
- **Dependencies**: All client functions, `split_ws()` from proto.cpp
- **Commands handled**: create_user, login, create_group, join_group, leave_group, list_groups, list_requests, accept_request, upload_file, list_files, download_file, show_downloads, stop_share, logout, quit

---

## 11. Feature-to-Function Mapping

### 11.1 User Registration Feature

**Functions Involved:**
1. **Client**: `main()` → Reads user command, calls `tracker_roundtrip()`
2. **Client**: `tracker_roundtrip()` → Uses `send_to_endpoint()` to send "REGISTER user pass"
3. **Client**: `send_to_endpoint()` → Uses `send_msg()` and `recv_msg()` from `proto.cpp`
4. **Tracker**: `serve_client()` → Receives via `recv_msg()`, calls `handle_command()`
5. **Tracker**: `handle_command()` → Validates, creates user, calls `save()`, calls `broadcast_sync()`
6. **Tracker**: `save()` → Writes user to disk
7. **Tracker**: `broadcast_sync()` → Spawns thread, calls `fire_and_forget()`
8. **Tracker**: `fire_and_forget()` → Sends sync message to other tracker
9. **Other Tracker**: `handle_command()` → Receives SYNC, calls `handle_sync()`
10. **Other Tracker**: `handle_sync()` → Applies same state change, calls `save()`

**Flow Diagram:**
```
main() → tracker_roundtrip() → send_to_endpoint() → send_msg()/recv_msg()
                                                              ↓
serve_client() → handle_command() → save() + broadcast_sync()
                                      ↓              ↓
                                      disk        fire_and_forget()
                                                     ↓
                                   Other tracker: handle_sync() → save()
```

### 11.2 User Login Feature

**Functions Involved:**
1. **Client**: `main()` → Reads command, calls `tracker_roundtrip()`
2. **Client**: `tracker_roundtrip()` → Communicates with tracker
3. **Tracker**: `handle_command()` → Validates password, sets `logged=true`, calls `save()`
4. **Client**: `main()` → Stores `current_user` on successful login

**Simpler flow** - no synchronization needed (login state is session-local)

### 11.3 Group Creation Feature

**Functions Involved:**
1. **Client**: `main()` → Parses command, validates user logged in
2. **Client**: `tracker_roundtrip()` → Sends "CREATE_GROUP user group"
3. **Tracker**: `handle_command()` → Creates group, adds user as owner+member, calls `save()`, calls `broadcast_sync()`
4. **Tracker**: Sync to other tracker (same as registration flow)

**Key**: User automatically becomes owner and first member

### 11.4 Group Join Request Feature

**Functions Involved:**
1. **Client**: `main()` → Sends "JOIN_GROUP user group"
2. **Tracker**: `handle_command()` → Checks group exists, adds request to `requests[group]` vector, calls `save()`, calls `broadcast_sync()`
3. Sync to other tracker

**Key**: Request stored in vector, awaiting owner approval

### 11.5 Accept Join Request Feature

**Functions Involved:**
1. **Client**: `main()` → Owner sends "ACCEPT_REQUEST group user"
2. **Tracker**: `handle_command()` → Checks owner permission via `is_owner()`, removes from requests vector, adds to members set, calls `save()`, calls `broadcast_sync()`
3. Sync to other tracker

**Permissions**: Only owner can accept (checked by `is_owner()`)

### 11.6 File Upload Feature

**Functions Involved:**
1. **Client**: `main()` → Parses "upload_file group path"
2. **Client**: `compute_piece_and_file_sha1()` → 
   - Opens file, calculates size
   - For each piece: reads data, calls `sha1_hex()` to hash
   - Concatenates hashes, calls `sha1_hex()` again for file hash
3. **Client**: `main()` → Adds file to `uploaded_files` map (thread-safe)
4. **Client**: `main()` → Constructs "UPLOAD_META" message with all hashes
5. **Client**: `tracker_roundtrip()` → Sends to tracker
6. **Tracker**: `handle_command()` → 
   - Validates user is member via `is_member()`
   - Creates File structure with metadata
   - Adds peer address to file's peer set
   - Calls `save()`, calls `broadcast_sync()`
7. Sync to other tracker

**Key Functions:**
- `sha1_hex()` (from sha1.cpp): Computes hashes
- `compute_piece_and_file_sha1()`: Orchestrates hashing process
- File data stays on client, only metadata sent to tracker

### 11.7 File Download Feature

**Functions Involved:**
1. **Client**: `main()` → Parses "download_file group filename dest"
2. **Client**: `tracker_roundtrip()` → Sends "GET_FILE_PEERS group filename user"
3. **Tracker**: `handle_command()` → 
   - Validates membership via `is_member()`
   - Returns file metadata: size, piece count, file hash, piece hashes, peer list
4. **Client**: `main()` → 
   - Parses response using `parse_hashes()`
   - Creates output file, pre-allocates size
   - Calls `run_download_job()` (in thread if background)
5. **Client**: `run_download_job()` → 
   - Creates DownloadStatus
   - Downloads pieces in batches (MAX_SIM_PIECES = 8 at a time)
   - For each piece, spawns thread calling `fetch_one_piece()`
6. **Client**: `fetch_one_piece()` → 
   - Connects to peer
   - Sends "GETPIECE filename index" via `send_msg()`
   - Receives piece data via `recv_all()`
   - Verifies hash via `sha1_hex()`
   - Writes to file at correct position
7. **Peer**: `peer_server_thread()` → 
   - Receives request via `recv_msg()`
   - Looks up file in `uploaded_files` map
   - Reads piece from file
   - Sends piece via `send_msg()` and `send_all()`
8. **Client**: `run_download_job()` → 
   - When complete, verifies file via `compute_piece_and_file_sha1()`
   - If valid, sends ADD_PEER to tracker
   - Adds file to `uploaded_files` map

**Key Functions:**
- `parse_hashes()`: Parses piece hash list
- `run_download_job()`: Coordinates parallel downloads
- `fetch_one_piece()`: Downloads single piece with verification
- `peer_server_thread()`: Serves pieces to other peers
- `sha1_hex()`: Verifies piece and file integrity

### 11.8 Multi-Tracker Failover Feature

**Functions Involved:**
1. **Client**: `tracker_roundtrip()` → 
   - Tries `connected_tracker` first via `send_to_endpoint()`
   - If fails, loops through all trackers
   - Updates `connected_tracker` when finds working one
2. **Client**: `send_to_endpoint()` → Sets timeouts, handles connection failures gracefully

**Key**: Transparent to user - automatic switching on failure

### 11.9 Tracker Synchronization Feature

**Functions Involved:**
1. **Tracker**: `handle_command()` → After state change, calls `broadcast_sync()`
2. **Tracker**: `broadcast_sync()` → Spawns thread, calls `fire_and_forget()` for each other tracker
3. **Tracker**: `fire_and_forget()` → Sends "SYNC <command>" via `send_msg()`, receives ACK
4. **Other Tracker**: `serve_client()` → Receives SYNC message
5. **Other Tracker**: `handle_command()` → Recognizes SYNC, calls `handle_sync()`
6. **Other Tracker**: `handle_sync()` → Parses command, applies same state change, calls `save()`

**Key**: Asynchronous sync doesn't block client response

### 11.10 Peer Server Feature

**Functions Involved:**
1. **Client**: `main()` → Calls `start_peer_server()` at startup
2. **Client**: `start_peer_server()` → Finds port, spawns `peer_server_thread()`
3. **Client**: `peer_server_thread()` → 
   - Listens for connections
   - Spawns worker thread per request
   - Worker: receives request, serves piece, closes connection

**Purpose**: Allow client to serve file pieces to other peers

---

## 12. Function Call Relationships

### 12.1 Client Upload Flow Call Graph

```
main()
  ├─→ split_ws() [proto.cpp]
  ├─→ compute_piece_and_file_sha1()
  │     ├─→ fopen() [file I/O]
  │     ├─→ sha1_hex() [sha1.cpp]
  │     │     └─→ sha1() [sha1.cpp]
  │     └─→ fclose() [file I/O]
  ├─→ tracker_roundtrip()
  │     └─→ send_to_endpoint()
  │           ├─→ socket() [syscall]
  │           ├─→ connect() [syscall]
  │           ├─→ send_msg() [proto.cpp]
  │           │     └─→ send_all() [proto.cpp]
  │           ├─→ recv_msg() [proto.cpp]
  │           │     └─→ recv_all() [proto.cpp]
  │           └─→ close() [syscall]
  └─→ lock_guard (uploaded_files mutex)
```

### 12.2 Client Download Flow Call Graph

```
main()
  ├─→ parse_download_cmd()
  ├─→ tracker_roundtrip()
  │     └─→ send_to_endpoint() [as above]
  ├─→ parse_hashes()
  ├─→ run_download_job() [in thread if background]
  │     ├─→ make_shared<DownloadStatus>()
  │     ├─→ thread() [for each piece batch]
  │     │     └─→ fetch_one_piece()
  │     │           ├─→ socket(), connect()
  │     │           ├─→ send_msg() → send_all()
  │     │           ├─→ recv_msg() → recv_all()
  │     │           ├─→ sha1_hex() → sha1()
  │     │           ├─→ open(), lseek(), write()
  │     │           └─→ close()
  │     ├─→ thread.join() [wait for batch]
  │     ├─→ compute_piece_and_file_sha1() [verification]
  │     └─→ tracker_roundtrip() [ADD_PEER]
```

### 12.3 Tracker Command Processing Call Graph

```
main()
  ├─→ load()
  │     ├─→ ifstream [file I/O]
  │     └─→ Parsing and data structure population
  ├─→ socket(), bind(), listen()
  └─→ accept() [loop]
        └─→ thread(serve_client)
              ├─→ recv_msg() → recv_all()
              ├─→ split_ws() [proto.cpp]
              └─→ handle_command()
                    ├─→ lock_guard(mtx)
                    ├─→ is_member() or is_owner() [permission checks]
                    ├─→ Data structure modifications
                    ├─→ save()
                    │     ├─→ mkdir()
                    │     └─→ ofstream [file I/O]
                    ├─→ send_msg() → send_all()
                    └─→ broadcast_sync()
                          └─→ thread()
                                └─→ fire_and_forget()
                                      ├─→ socket(), connect()
                                      ├─→ send_msg() → send_all()
                                      ├─→ recv_msg() → recv_all()
                                      └─→ close()
```

### 12.4 Tracker Synchronization Call Graph

```
Tracker 0: handle_command()
  └─→ broadcast_sync()
        └─→ thread()
              └─→ fire_and_forget()
                    └─→ [TCP connection to Tracker 1]

Tracker 1: serve_client()
  └─→ recv_msg() → recv_all()
  └─→ handle_command()
        └─→ handle_sync()
              ├─→ Parsing sync_data
              ├─→ Data structure modifications
              └─→ save()
```

### 12.5 Peer-to-Peer Piece Transfer Call Graph

```
Downloader Client:
  fetch_one_piece()
    ├─→ socket(), connect()
    ├─→ send_msg() → send_all()
    ├─→ recv_msg() → recv_all() ["OK"]
    ├─→ recv_all() [piece size]
    ├─→ recv_all() [piece data]
    ├─→ sha1_hex() → sha1() [verification]
    ├─→ open(), lseek(), write()
    └─→ close()

Uploader Client:
  peer_server_thread()
    └─→ accept() [loop]
          └─→ thread()
                ├─→ recv_msg() → recv_all() ["GETPIECE ..."]
                ├─→ split_ws() [proto.cpp]
                ├─→ fopen(), fseek(), fread()
                ├─→ send_msg() → send_all() ["OK"]
                ├─→ send_all() [piece size]
                ├─→ send_all() [piece data]
                └─→ close()
```

---

## 13. How Functions Collaborate to Implement Features

### 13.1 Feature: User Registration with Synchronization

**Collaboration Pattern:**
- **Client side**: `main()` (UI) → `tracker_roundtrip()` (reliability) → `send_to_endpoint()` (connection) → `send_msg()`/`recv_msg()` (protocol)
- **Tracker side**: `serve_client()` (network) → `handle_command()` (business logic) → `save()` (persistence) → `broadcast_sync()` (replication)
- **Synchronization**: `broadcast_sync()` → `fire_and_forget()` → other tracker's `handle_sync()` → `save()`

**Why this works:**
- Each function has single responsibility
- Protocol layer (`send_msg`/`recv_msg`) handles reliable transmission
- Business logic (`handle_command`) validates and updates state
- Persistence (`save`) ensures durability
- Synchronization (`broadcast_sync`/`handle_sync`) ensures consistency

### 13.2 Feature: Parallel File Download with Integrity

**Collaboration Pattern:**
- **Orchestration**: `run_download_job()` manages overall download
- **Piece Download**: `fetch_one_piece()` handles single piece (called in parallel threads)
- **Integrity**: `sha1_hex()` verifies each piece before writing
- **Serving**: `peer_server_thread()` on uploader side serves pieces

**Why this works:**
- `run_download_job()` coordinates without blocking
- Multiple `fetch_one_piece()` threads run concurrently
- Each piece independently verified (early error detection)
- Atomic operations (`remaining`) track progress thread-safely
- Mutex protects shared `have` vector from race conditions

###  13.3 Feature: Multi-Tracker High Availability

**Collaboration Pattern:**
- **Failover Logic**: `tracker_roundtrip()` tries multiple trackers
- **Connection Management**: `send_to_endpoint()` handles timeouts and failures
- **State Replication**: `broadcast_sync()` ensures both trackers have same state

**Why this works:**
- Client doesn't need to know which tracker is primary
- Automatic switching on failure (transparent to user)
- Both trackers maintain identical state through sync
- If one tracker fails, client continues with other

### 13.4 Feature: Peer Discovery and Direct Transfer

**Collaboration Pattern:**
- **Discovery**: `tracker_roundtrip("GET_FILE_PEERS")` gets peer list
- **Transfer**: `fetch_one_piece()` connects directly to peer
- **Serving**: `peer_server_thread()` responds to piece requests

**Why this works:**
- Tracker only provides metadata (lightweight)
- Actual data transfer is P2P (decentralized, scalable)
- Each peer can serve pieces while downloading others
- Network load distributed across peers, not central servers

### 13.5 Feature: File Integrity Verification

**Collaboration Pattern:**
- **Hash Generation**: `compute_piece_and_file_sha1()` creates hashes during upload
- **Hash Storage**: Tracker stores hashes in File structure
- **Hash Distribution**: Tracker sends hashes to downloaders
- **Hash Verification**: `fetch_one_piece()` verifies each piece, `run_download_job()` verifies complete file

**Why this works:**
- Two-level verification (piece + file) catches errors at multiple granularities
- Piece verification happens immediately (no need to verify entire file to catch corruption)
- File verification ensures complete file integrity after assembly
- SHA1 algorithm ensures cryptographic-level integrity checking

