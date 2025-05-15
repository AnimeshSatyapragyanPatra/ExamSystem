# 📝 ExamSphere – Terminal-Based Digital Examination System

**ExamSphere** is a C language-based digital examination platform that runs through the terminal. Designed with a client-server architecture, it ensures secure, efficient, and fair assessments for both students and instructors.

## 🚀 Features

- ✅ **Built with C** – Lightweight and fast.
- 🌐 **Client-Server Architecture** – Mimics real-world exam systems.
- 👨‍🏫 **Instructor Access** – Special privileges for managing exams.
- ❌ **Anti-Cheating System** – Flags suspicious activity automatically.
- 📁 **Structured Data Files** – Clean management of students, questions, and results.

## 🧱 Components

| File / Module           | Description                                  |
|-------------------------|----------------------------------------------|
| `student_dtls.txt`      | Stores student login and identity details    |
| `instructor_dtls.txt`   | Instructor credentials and permissions       |
| `questions.txt`         | Instructor-defined exam questions            |
| `rules.txt`             | Exam guidelines visible to all               |
| `result.txt`            | Auto-generated after exam submission         |
| `client.c`              | Client-side code for student/instructor      |
| `server.c`              | Server-side code to handle requests          |

## 🔧 How It Works

1. **Server** initializes and listens for connections.
2. **Client** connects from terminal (student/instructor).
3. Based on role:
   - **Instructor** can add/view questions, rules, and results.
   - **Student** can read rules, attend exam, and view result.
4. Cheating attempts are monitored and flagged.

## 🖥️ Requirements

- GCC Compiler
- Linux OS (recommended)
- Basic terminal knowledge

