
<div align="center">

![cash Banner](./cash-logo-main.png)

# cash - A Simple Command and Script Shell
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=for-the-badge&logo=appveyor)](LICENSE) [![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=for-the-badge&logo=travis)]() [![Platform](https://img.shields.io/badge/platform-linux%20%7C%20macOS-lightgrey?style=for-the-badge&logo=linux)]() [![Shell](https://img.shields.io/badge/shell-cash%20v0.1-yellow?style=for-the-badge&logo=gnu-bash)]()

---

**cash** (*Command and Script Shell*) is a simple shell built in C to explore **kernel processes, system calls, and shell internals**. This project serves as a **learning exercise** in understanding how shells interact with the OS, manage processes, and execute commands.

---

## 🚀 Features
✅ Minimalist and lightweight shell implementation  
✅ Command execution with argument parsing  
✅ Process management using system calls  
✅ Basic built-in commands (`cd`, `exit`, etc.)  
✅ Simple scripting capabilities (to be added)  

*(More features will be added as development progresses!)*

---

## 🛠 Installation

### 1️⃣ Clone the Repository
```bash
 git clone https://github.com/yourusername/cash.git
 cd cash
```

### 2️⃣ Build the Shell
```bash
 make
```

### 3️⃣ Run CaSh
```bash
 ./cash
```

*(Optional: Move to `/usr/local/bin` for global access)*
```bash
 sudo cp cash /usr/local/bin
```

---

## 📌 Usage
Start CaSh by running:
```bash
 cash
```
Run standard commands like:
```bash
 ls -l
 echo "Hello, CaSh!"
 pwd
```
To exit, type:
```bash
 exit
```

---

## ✅ Common System Commands (that work right now in `ca$h`):

### **File & Directory Operations**
```bash
ls           # List files in the current directory
pwd          # Show current directory path
mkdir dir    # Create a new directory
rmdir dir    # Remove a directory
rm file.txt  # Delete a file
cp file1 file2  # Copy files
mv old new   # Rename or move files
find . -name "*.txt"  # Search for files
touch file   # Create an empty file
stat file    # Get file details
```

### **Process & System Information**
```bash
ps           # Show running processes
top          # Show system stats
htop         # (if installed) Interactive process viewer
whoami       # Show the current user
id           # Show user ID
uptime       # Show how long the system has been running
uname -a     # Show system info
hostname     # Show computer name
date         # Show current date and time
cal          # Show a calendar
df -h        # Show disk usage
du -sh *     # Show size of directories
free -m      # Show memory usage
```

### **Text Processing & File Viewing**
```bash
cat file.txt     # View file content
less file.txt    # View file with scrolling
more file.txt    # View file page by page
head -n 10 file  # Show first 10 lines
tail -n 10 file  # Show last 10 lines
grep "word" file # Search for "word" in file
wc -l file       # Count lines in a file
awk '{print $1}' file  # Process text in a file
sed 's/old/new/g' file # Replace text in a file
```

### **Networking & Internet**
```bash
ping google.com     # Check internet connection
curl https://site.com  # Download a webpage
wget https://file.com  # Download a file
nslookup google.com # Get DNS information
traceroute google.com # Trace network path
```

### **User Management**
```bash
who       # Show logged-in users
w         # Show detailed user info
groups    # Show groups of the current user
passwd    # Change password (may require sudo)
```

### **Archiving & Compression**
```bash
tar -cvf archive.tar file1 file2  # Create a tar archive
tar -xvf archive.tar  # Extract a tar archive
zip archive.zip file  # Create a zip archive
unzip archive.zip      # Extract a zip archive
gzip file              # Compress a file
gunzip file.gz         # Decompress a file
```

### **Compiling & Development**
```bash
gcc program.c -o program  # Compile a C program
g++ program.cpp -o program # Compile a C++ program
make            # Run Makefile
python3 script.py  # Run a Python script
node script.js  # Run a Node.js script
javac Main.java && java Main  # Compile and run Java
```

### **Package Management (Depends on OS)**
```bash
brew install package  # (macOS) Install a package
apt install package   # (Linux) Install a package
pacman -S package     # (Arch Linux) Install a package
```

### **Graphical & macOS-Specific Commands**
✅ If you are on **macOS**, you can run:
```bash
open .                 # Open Finder in current directory
open -a "Google Chrome" # Open Chrome browser
say "Hello, world"      # Text-to-speech
```
✅ If you are on **Linux**, try:
```bash
xdg-open .             # Open file manager
```

---

## 🛣 Roadmap
- [ ] Implement command history
- [ ] Add support for piping (`|`) and redirection (`>`, `<`)
- [ ] Implement background and foreground process handling
- [ ] Add basic scripting support (`.cash` files)
- [ ] Create a configuration file (`.cashrc` for aliases, env vars, etc.)

---

## 🤝 Contributing
Contributions are **welcome**! If you’d like to add features or fix bugs:
1. **Fork** the repository
2. **Create a feature branch** (`git checkout -b feature-name`)
3. **Commit your changes** (`git commit -m "Add cool feature"`)
4. **Push to your branch** (`git push origin feature-name`)
5. **Create a Pull Request**

---

## List of supported commands (till now):



## 📜 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---
</div>