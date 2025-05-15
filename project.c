#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>
#include <termios.h>

#define MAX_QUESTIONS 200
#define MAX_LINE 512
#define MAX_STUDENTS 100
#define STUDENT_FILE "student_dtls.txt"
#define INSTRUCTOR_FILE "instructor_dtls.txt"
#define QUESTION_FILE "questions_with_difficulty.txt"
#define RESULT_FILE "results.txt"
#define RULES_FILE "rules.txt"
#define NUM_EXAM_QUESTIONS 5
#define MIN_ANSWER_TIME 5

volatile int examTimeUp = 0;
int overallExamTime = 300;

typedef struct {
    char name[MAX_LINE];
    char roll[MAX_LINE];
    char reg_no[MAX_LINE];
    char password[MAX_LINE];
} Student;

typedef struct {
    char name[MAX_LINE];
    char instructor_id[MAX_LINE];
    char password[MAX_LINE];
} Instructor;

typedef struct {
    char question[MAX_LINE];
    char optionA[MAX_LINE];
    char optionB[MAX_LINE];
    char optionC[MAX_LINE];
    char optionD[MAX_LINE];
    char correct;
    int difficulty;
} Question;

typedef struct {
    char roll[MAX_LINE];
    char name[MAX_LINE];
    int responseTimes[NUM_EXAM_QUESTIONS];
    int totalTime;
    int correctAnswers;
    int totalQuestions;
    int rank;
    int flagged;
} DashboardStudent;

DashboardStudent dashboardStudents[MAX_STUDENTS];
int studentCount = 0;

Question questions[MAX_QUESTIONS];
int totalQuestions = 0;
int answerTimeout;
float marksForCorrectAnswer;
float marksDeductedForWrongAnswer;

// Function prototypes
void clear_input_buffer();
void getPassword(char *password, int size);
void load_rules();
void trim(char *s);
int read_nonempty_line(FILE *fp, char *buffer, int size);
void load_questions();
int verify_student(const char *roll, const char *pass, char *name, char *reg_no);
int verify_instructor(const char *instructor_id, const char *pass, char *name);
void append_result(const char *name, const char *roll, double score, int wrong, int attempted, int isCheating, const int *responseTimes, int totalTime);
int get_input_with_timeout(char *buf, int buf_size, int timeout_seconds);
void *overall_timer(void *arg);
void *exam_session(void *arg);
void add_question();
void set_time_limit();
void set_marking_scheme();
void loadDashboardData();
void flagSuspiciousActivity();
void rankStudents();
void displayDashboard();

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void getPassword(char *password, int size) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    if(fgets(password, size, stdin) != NULL) {
        password[strcspn(password, "\n")] = '\0';
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

void load_rules() {
    FILE *fp = fopen(RULES_FILE, "r");
    if(fp == NULL) {
        answerTimeout = 30;
        marksForCorrectAnswer = 1.0;
        marksDeductedForWrongAnswer = 0.25;
        fp = fopen(RULES_FILE, "w");
        fprintf(fp, "Time limit per question: %d\nMarks awarded for correct answer: %.2f\nMarks deducted for incorrect answer: %.2f\n", 
                answerTimeout, marksForCorrectAnswer, marksDeductedForWrongAnswer);
        fclose(fp);
    } else {
        char buffer[MAX_LINE];
        fgets(buffer, MAX_LINE, fp);
        sscanf(buffer, "Time limit per question: %d", &answerTimeout);
        fgets(buffer, MAX_LINE, fp);
        sscanf(buffer, "Marks awarded for correct answer: %f", &marksForCorrectAnswer);
        fgets(buffer, MAX_LINE, fp);
        sscanf(buffer, "Marks deducted for incorrect answer: %f", &marksDeductedForWrongAnswer);
        fclose(fp);
    }
}

void trim(char *s) {
    s[strcspn(s, "\n")] = '\0';
    while(isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while(end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

int read_nonempty_line(FILE *fp, char *buffer, int size) {
    while(fgets(buffer, size, fp)) {
        buffer[strcspn(buffer, "\n")] = '\0';
        char temp[MAX_LINE];
        strcpy(temp, buffer);
        char *start = temp;
        while(*start && isspace((unsigned char)*start)) start++;
        if(strlen(start) > 0) {
            strcpy(buffer, start);
            return 1;
        }
    }
    return 0;
}

void load_questions() {
    FILE *fp = fopen(QUESTION_FILE, "r");
    if(fp == NULL) {
        perror("📛 Error opening questions file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    int qIndex = 0;
    while(qIndex < MAX_QUESTIONS) {
        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        strncpy(questions[qIndex].question, line, MAX_LINE);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        strncpy(questions[qIndex].optionA, line, MAX_LINE);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        strncpy(questions[qIndex].optionB, line, MAX_LINE);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        strncpy(questions[qIndex].optionC, line, MAX_LINE);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        strncpy(questions[qIndex].optionD, line, MAX_LINE);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        questions[qIndex].correct = toupper(line[0]);

        if(!read_nonempty_line(fp, line, MAX_LINE)) break;
        questions[qIndex].difficulty = atoi(line);

        qIndex++;
    }

    totalQuestions = qIndex;
    fclose(fp);
}

int verify_student(const char *roll, const char *pass, char *name, char *reg_no) {
    FILE *fp = fopen(STUDENT_FILE, "r");
    if(fp == NULL) {
        perror("📛 Error opening student details file");
        exit(EXIT_FAILURE);
    }
    char fileName[50], fileRoll[50], fileRegNo[50], filePass[50];
    int valid = 0;
    while(fscanf(fp, "%s %s %s %s", fileName, fileRoll, fileRegNo, filePass) == 4) {
        if(strcmp(fileRoll, roll) == 0 && strcmp(filePass, pass) == 0) {
            strcpy(name, fileName);
            strcpy(reg_no, fileRegNo);
            valid = 1;
            break;
        }
    }
    fclose(fp);
    return valid;
}

int verify_instructor(const char *instructor_id, const char *pass, char *name) {
    FILE *fp = fopen(INSTRUCTOR_FILE, "r");
    if(fp == NULL) {
        perror("📛 Error opening instructor details file");
        exit(EXIT_FAILURE);
    }
    char fileName[50], fileInstructorID[50], filePass[50];
    int valid = 0;
    while(fscanf(fp, "%s %s %s", fileName, fileInstructorID, filePass) == 3) {
        if(strcmp(fileInstructorID, instructor_id) == 0 && strcmp(filePass, pass) == 0) {
            strcpy(name, fileName);
            valid = 1;
            break;
        }
    }
    fclose(fp);
    return valid;
}

void append_result(const char *name, const char *roll, double score, int wrong, int attempted, int isCheating, const int *responseTimes, int totalTime) {
    FILE *fp = fopen(RESULT_FILE, "a");
    if(fp == NULL) {
        perror("📛 Error opening result file");
        return;
    }
    int fd = fileno(fp);
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    fcntl(fd, F_SETLKW, &lock);

    fprintf(fp, "%s|%s|%.2f|%d|%d|%d|%d|", 
           roll, name, score, wrong, attempted, isCheating, totalTime);
    
    for(int i = 0; i < attempted; i++) {
        fprintf(fp, "%d,", responseTimes[i]);
    }
    fprintf(fp, "\n");

    lock.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &lock);
    fclose(fp);
}

int get_input_with_timeout(char *buf, int buf_size, int timeout_seconds) {
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;
    
    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    if(rv == -1) {
        perror("select");
        return 0;
    } else if(rv == 0) {
        return 0;
    } else {
        if(fgets(buf, buf_size, stdin) == NULL) {
            return 0;
        }
        buf[strcspn(buf, "\n")] = '\0';
        return 1;
    }
}

void *overall_timer(void *arg) {
    sleep(overallExamTime);
    examTimeUp = 1;
    printf("\n⏰ *** Overall exam time is up! The exam will now end. ***\n");
    pthread_exit(NULL);
}

void *exam_session(void *arg) {
    char *roll = (char *)arg;
    char name[MAX_LINE], reg_no[MAX_LINE];
    double weightedScore = 0.0;
    int totalDifficulty = 0;
    int wrongCount = 0, attempted = 0;
    int isCheating = 0;
    time_t questionStartTime;
    int totalAnswerTime = 0;
    int responseTimes[NUM_EXAM_QUESTIONS] = {0};
    
    int correctByDifficulty[4] = {0};
    int attemptedByDifficulty[4] = {0};
    int timeByDifficulty[4] = {0};
    char* diffNames[] = {"", "⭐ Easy", "⭐⭐ Medium", "⭐⭐⭐ Hard"};
    float diffWeights[] = {0, 1.0, 1.5, 2.0};

    int indices[totalQuestions];
    for (int i = 0; i < totalQuestions; i++)
        indices[i] = i;
    srand(time(NULL));
    for (int i = totalQuestions - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
    
    printf("\n📝 Exam starting now. You will be shown %d questions.\n", NUM_EXAM_QUESTIONS);
    printf("⏱️  You have %d seconds per question.\n", answerTimeout);
    printf("⏳ Overall exam time: %d seconds.\n", overallExamTime);
    printf("💡 Question weights: Easy(x%.1f) Medium(x%.1f) Hard(x%.1f)\n", 
           diffWeights[1], diffWeights[2], diffWeights[3]);
    printf("🚪 Enter 'e' at any time to exit the exam.\n\n");
    
    char answerBuf[20];
    for (int i = 0; i < NUM_EXAM_QUESTIONS && i < totalQuestions; i++) {
        if (examTimeUp) {
            printf("\n⏰ Overall exam time has expired.\n");
            break;
        }
        
        Question *q = &questions[indices[i]];
        totalDifficulty += q->difficulty;

        printf("\n--------------------------------------------------\n");
        printf("| 🔹 Q%-38d | %-12s |\n", i+1, diffNames[q->difficulty]);
        printf("--------------------------------------------------\n");
        printf("| %-47s |\n", q->question);
        printf("| 🅰️  %-45s |\n", q->optionA);
        printf("| 🅱️  %-45s |\n", q->optionB);
        printf("| ©️  %-45s |\n", q->optionC);
        printf("| 🅳  %-45s |\n", q->optionD);
        printf("--------------------------------------------------\n");
        
        printf("💭 Your answer (A/B/C/D or 'e' to exit): ");
        fflush(stdout);
        
        questionStartTime = time(NULL);
        int gotInput = get_input_with_timeout(answerBuf, sizeof(answerBuf), answerTimeout);
        int answerTime = time(NULL) - questionStartTime;
        totalAnswerTime += answerTime;
        timeByDifficulty[q->difficulty] += answerTime;
        responseTimes[i] = answerTime;
        
        clear_input_buffer();
        
        if(!gotInput) {
            printf("\n⏰ Time's up for this question! No answer provided.\n");
            wrongCount++;
            attempted++;
            attemptedByDifficulty[q->difficulty]++;
            printf("\n--------------------------\n\n");
            continue;
        }
        
        if(answerBuf[0] == 'e' || answerBuf[0] == 'E') {
            printf("\n🚪 Exiting exam early...\n");
            break;
        }
        
        char userAns = toupper(answerBuf[0]);
        attempted++;
        attemptedByDifficulty[q->difficulty]++;
        
        if(answerTime < MIN_ANSWER_TIME) {
            printf("\n⚠️  Warning: You answered very quickly (%d seconds).\n", answerTime);
            isCheating = 1;
        }
        
        if(userAns == q->correct) {
            printf("✅ Correct! (+%.1f points)\n", diffWeights[q->difficulty]);
            weightedScore += diffWeights[q->difficulty];
            correctByDifficulty[q->difficulty]++;
        } else {
            printf("❌ Wrong! Correct answer: %c\n", q->correct);
            wrongCount++;
        }
        printf("\n--------------------------\n\n");
    }
    
    if(attempted > 0 && (totalAnswerTime/attempted) < MIN_ANSWER_TIME) {
        isCheating = 1;
    }
    
    int correctCount = attempted - wrongCount;
    double accuracy = (attempted > 0) ? ((double)correctCount / attempted) * 100 : 0;
    
    printf("\n***********************************************************************\n");
    printf("*                         📊 DETAILED RESULTS                        *\n");
    printf("***********************************************************************\n");
    printf("| %-25s: %10.2f (Max: %.1f)                   |\n", "🎯 Weighted Score", weightedScore, 
          NUM_EXAM_QUESTIONS * diffWeights[3]);
    printf("| %-25s: %10.2f%%                                   |\n", "📈 Overall Accuracy", accuracy);
    
    printf("\n--------------------------------------------------------\n");
    printf("| %-12s | %-8s | %-8s | %-8s | %-8s |\n", "Difficulty", "Correct", "Attempted", "Accuracy", "Avg Time");
    printf("--------------------------------------------------------\n");
    
    for(int i = 1; i <= 3; i++) {
        if(attemptedByDifficulty[i] > 0) {
            float diffAccuracy = (float)correctByDifficulty[i]/attemptedByDifficulty[i]*100;
            float avgTime = (float)timeByDifficulty[i]/attemptedByDifficulty[i];
            
            printf("| %-12s | %-8d | %-8d | %-7.1f%% | %-7.1fs |\n",
                  diffNames[i],
                  correctByDifficulty[i],
                  attemptedByDifficulty[i],
                  diffAccuracy,
                  avgTime);
        }
    }
    
    printf("--------------------------------------------------------\n");
    printf("| %-25s: %10d                                     |\n", "📝 Total Attempted", attempted);
    printf("***********************************************************************\n\n");
    
    append_result(name, roll, weightedScore, wrongCount, attempted, isCheating, responseTimes, totalAnswerTime);
    pthread_exit(NULL);
}

void loadDashboardData() {
    FILE *file = fopen(RESULT_FILE, "r");
    if (file == NULL) return;

    studentCount = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file) && studentCount < MAX_STUDENTS) {
        DashboardStudent *s = &dashboardStudents[studentCount];
        char *token = strtok(line, "|");
        
        strcpy(s->roll, token);
        token = strtok(NULL, "|");
        strcpy(s->name, token);
        token = strtok(NULL, "|");
        s->correctAnswers = atoi(token);
        token = strtok(NULL, "|");
        s->totalQuestions = atoi(token);
        token = strtok(NULL, "|");
        s->flagged = atoi(token);
        token = strtok(NULL, "|");
        s->totalTime = atoi(token);
        
        char *timeToken = strtok(token, ",");
        int i = 0;
        while (timeToken != NULL && i < NUM_EXAM_QUESTIONS) {
            s->responseTimes[i++] = atoi(timeToken);
            timeToken = strtok(NULL, ",");
        }
        
        studentCount++;
    }
    fclose(file);
}

void flagSuspiciousActivity() {
    for (int i = 0; i < studentCount; ++i) {
        for (int j = 0; j < dashboardStudents[i].totalQuestions; ++j) {
            if (dashboardStudents[i].responseTimes[j] < 2) {
                dashboardStudents[i].flagged = 1;
                break;
            }
        }
    }
}

void rankStudents() {
    for (int i = 0; i < studentCount - 1; ++i) {
        for (int j = 0; j < studentCount - i - 1; ++j) {
            if (dashboardStudents[j].correctAnswers < dashboardStudents[j + 1].correctAnswers) {
                DashboardStudent temp = dashboardStudents[j];
                dashboardStudents[j] = dashboardStudents[j + 1];
                dashboardStudents[j + 1] = temp;
            }
        }
    }

    for (int i = 0; i < studentCount; ++i) {
        dashboardStudents[i].rank = i + 1;
    }
}

void displayDashboard() {
    loadDashboardData();
    flagSuspiciousActivity();
    rankStudents();
    
    printf("\n\n--------------------------------------------------\n");
    printf("| Rank | Name         | Total Time | Accuracy | Flagged |\n");
    printf("--------------------------------------------------\n");

    for (int i = 0; i < studentCount; ++i) {
        float accuracy = dashboardStudents[i].totalQuestions > 0 ? 
            (float)dashboardStudents[i].correctAnswers / dashboardStudents[i].totalQuestions * 100 : 0;
        
        printf("| %-4d | %-12s | %-10d | %-8.2f | %-7s |\n",
               dashboardStudents[i].rank,
               dashboardStudents[i].name,
               dashboardStudents[i].totalTime,
               accuracy,
               dashboardStudents[i].flagged ? "🚩" : "✅");
    }
    printf("--------------------------------------------------\n");
}

void add_question() {
    FILE *fp = fopen(QUESTION_FILE, "a");
    if(fp == NULL) {
        perror("📛 Error opening questions file");
        return;
    }
    
    Question newQuestion;
    printf("📝 Enter the question: ");
    clear_input_buffer();
    fgets(newQuestion.question, MAX_LINE, stdin);
    trim(newQuestion.question);
    
    printf("🅰️  Enter option A: ");
    fgets(newQuestion.optionA, MAX_LINE, stdin);
    trim(newQuestion.optionA);
    
    printf("🅱️  Enter option B: ");
    fgets(newQuestion.optionB, MAX_LINE, stdin);
    trim(newQuestion.optionB);
    
    printf("©️  Enter option C: ");
    fgets(newQuestion.optionC, MAX_LINE, stdin);
    trim(newQuestion.optionC);
    
    printf("🅳  Enter option D: ");
    fgets(newQuestion.optionD, MAX_LINE, stdin);
    trim(newQuestion.optionD);
    
    printf("✅ Enter the correct option (A/B/C/D): ");
    scanf(" %c", &newQuestion.correct);
    newQuestion.correct = toupper(newQuestion.correct);
    
    printf("📊 Enter difficulty level (1=Easy, 2=Medium, 3=Hard): ");
    scanf("%d", &newQuestion.difficulty);
    
    fprintf(fp, "%s\n%s\n%s\n%s\n%s\n%c\n%d\n", 
           newQuestion.question, 
           newQuestion.optionA,
           newQuestion.optionB,
           newQuestion.optionC,
           newQuestion.optionD,
           newQuestion.correct,
           newQuestion.difficulty);
    
    fclose(fp);
    printf("🎉 Question added successfully!\n");
}

void set_time_limit() {
    int new_time;
    printf("⏱️  Enter the new time limit for each question (in seconds): ");
    scanf("%d", &new_time);
    answerTimeout = new_time;
    
    FILE *fp = fopen(RULES_FILE, "w");
    if(fp != NULL) {
        fprintf(fp, "Time limit per question: %d\nMarks awarded for correct answer: %.2f\nMarks deducted for incorrect answer: %.2f\n", 
                answerTimeout, marksForCorrectAnswer, marksDeductedForWrongAnswer);
        fclose(fp);
    }
    printf("🔄 Time limit set to %d seconds.\n", answerTimeout);
}

void set_marking_scheme() {
    printf("➕ Enter marks for correct answer: ");
    scanf("%f", &marksForCorrectAnswer);
    printf("➖ Enter marks deducted for wrong answer: ");
    scanf("%f", &marksDeductedForWrongAnswer);
    
    FILE *fp = fopen(RULES_FILE, "w");
    if(fp != NULL) {
        fprintf(fp, "Time limit per question: %d\nMarks awarded for correct answer: %.2f\nMarks deducted for incorrect answer: %.2f\n", 
                answerTimeout, marksForCorrectAnswer, marksDeductedForWrongAnswer);
        fclose(fp);
    }
    printf("🔄 Marking scheme updated: +%.2f for correct, -%.2f for wrong.\n", 
           marksForCorrectAnswer, marksDeductedForWrongAnswer);
}

void instructor_menu() {
    int instructor_choice;
    do {
        printf("\n📋 Instructor Menu:\n");
        printf("1. ⏱️  Set Time Limit for Questions\n");
        printf("2. 📝 Add a Question\n");
        printf("3. 📊 Set Marking Scheme\n");
        printf("4. 📈 View Dashboard\n");
        printf("5. 🚪 Exit\n");
        printf("🎯 Enter your choice: ");
        scanf("%d", &instructor_choice);
        
        switch (instructor_choice) {
            case 1:
                set_time_limit();
                break;
            case 2:
                add_question();
                break;
            case 3:
                set_marking_scheme();
                break;
            case 4:
                displayDashboard();
                break;
            case 5:
                printf("\n🚪 Exiting...\n");
                break;
            default:
                printf("\n📛 Invalid choice! Please try again.\n");
        }
        clear_input_buffer();
    } while (instructor_choice != 5);
}

int main() {
    printf("\n\n✨✨✨ Welcome to ExamSys - Online Examination System ✨✨✨\n\n");
    load_rules();
    
    int choice;
    printf("👤 Are you a:\n1. Student\n2. Instructor\n🎯 Enter your choice (1/2): ");
    scanf("%d", &choice);
    
    if (choice == 1) {
        char roll[50], password[50];
        printf("\n🎓 Welcome to ExamSys Online MCQ Exam Platform\n");
        printf("📝 Enter Roll No: ");
        scanf("%s", roll);
        printf("🔒 Enter Password: ");
        clear_input_buffer();
        getPassword(password, sizeof(password));
        
        char name[MAX_LINE], reg_no[MAX_LINE];
        if(!verify_student(roll, password, name, reg_no)) {
            printf("📛 Invalid credentials! Exiting.\n");
            exit(EXIT_FAILURE);
        }
        
        printf("\n🎉 Login successful. Welcome, %s!\n", name);
        
        printf("\n====================================================\n");
        printf("| 📜          RULES FOR THE EXAM                 |\n");
        printf("====================================================\n");
        printf("| 🔹 Number of questions: 5                        |\n");
        printf("| ⏱️  Time per question: %-3d seconds                  |\n", answerTimeout);
        printf("| ➕ Marks for correct answer: %-4.2f                   |\n", marksForCorrectAnswer);
        printf("| ➖ Marks deducted for wrong answer: %-4.2f            |\n", marksDeductedForWrongAnswer);
        printf("====================================================\n");
        
        char ready;
        printf("\n🎯 Press 'R' when you are ready to start the exam: ");
        scanf(" %c", &ready);
        if (toupper(ready) != 'R') {
            printf("\n🚪 Exiting exam.\n");
            exit(EXIT_SUCCESS);
        }
        
        load_questions();
        if(totalQuestions == 0) {
            printf("\n📛 No questions loaded. Exiting.\n");
            exit(EXIT_FAILURE);
        }
        
        pthread_t overallTimerThread;
        if(pthread_create(&overallTimerThread, NULL, overall_timer, NULL) != 0) {
            perror("📛 Error creating overall timer thread");
            exit(EXIT_FAILURE);
        }
        
        pthread_t exam_thread;
        if(pthread_create(&exam_thread, NULL, exam_session, (void *)roll) != 0) {
            perror("📛 Error creating exam thread");
            exit(EXIT_FAILURE);
        }
        
        pthread_join(exam_thread, NULL);
        pthread_join(overallTimerThread, NULL);
    } else if (choice == 2) {
        char instructor_id[50], password[50], name[MAX_LINE];
        printf("\n👨‍🏫 Enter Instructor ID: ");
        scanf("%s", instructor_id);
        printf("🔒 Enter Password: ");
        clear_input_buffer();
        getPassword(password, sizeof(password));
        
        if(!verify_instructor(instructor_id, password, name)) {
            printf("\n📛 Invalid credentials! Exiting.\n");
            exit(EXIT_FAILURE);
        }
        
        printf("\n🎉 Login successful. Welcome, %s!\n", name);
        instructor_menu();
    } else {
        printf("\n📛 Invalid choice! Exiting.\n");
        exit(EXIT_FAILURE);
    }
    
    printf("\n✨ Thank you for using ExamSys! Goodbye! ✨\n");
    return 0;
}
