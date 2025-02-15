
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

## 📜 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---
</div>