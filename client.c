#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define MAX_LINE 512
#define NUM_EXAM_QUESTIONS 5
#define MIN_ANSWER_TIME 5
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

// Global flag to indicate if the overall exam time is up
volatile int examTimeUp = 0;

// Total time allowed for the whole exam in seconds
int overallExamTime = 300;

// Structure to represent a question
typedef struct {
    char question[MAX_LINE];
    char optionA[MAX_LINE];
    char optionB[MAX_LINE];
    char optionC[MAX_LINE];
    char optionD[MAX_LINE];
    char correct;        // Correct option (A/B/C/D)
    int difficulty;      // Difficulty level: 1 = Easy, 2 = Medium, 3 = Hard
} Question;

// Structure to hold exam results for a student
typedef struct {
    char roll[MAX_LINE];
    char name[MAX_LINE];
    int responseTimes[NUM_EXAM_QUESTIONS];
    int totalTime;
    int correctAnswers;
    int totalQuestions;
    int flagged;         // Indicates possible cheating
} ExamResult;

// Clears stdin buffer
void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

// Hides password input from screen
void getPassword(char *password, int size) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt); // Get current terminal attributes
    newt = oldt;
    newt.c_lflag &= ~(ECHO); // Disable echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    if (fgets(password, size, stdin) != NULL) {
        password[strcspn(password, "\n")] = '\0'; // Remove newline
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore terminal
    printf("\n");
}

// Waits for input with a timeout
int get_input_with_timeout(char *buf, int buf_size, int timeout_seconds) {
    fd_set set;
    struct timeval timeout;

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    if (rv == -1) {
        perror("📛 select");
        return 0;
    } else if (rv == 0) {
        return 0; // Timeout
    } else {
        if (fgets(buf, buf_size, stdin) == NULL) {
            return 0;
        }
        buf[strcspn(buf, "\n")] = '\0';
        return 1; // Successfully got input
    }
}

// Timer thread for the overall exam duration
void *overall_timer(void *arg) {
    sleep(overallExamTime);
    examTimeUp = 1;
    printf("\n⏰ *** Overall exam time is up! The exam will now end. ***\n");
    pthread_exit(NULL);
}

// Main function to conduct the exam
void conduct_exam(int sock, char *roll, char *name, Question *questions, int totalQuestions, int answerTimeout) {
    ExamResult result = {0};
    strcpy(result.roll, roll);
    strcpy(result.name, name);

    double weightedScore = 0.0;
    int wrongCount = 0, attempted = 0;
    int isCheating = 0;
    time_t questionStartTime;
    int totalAnswerTime = 0;

    // Arrays to track performance by difficulty
    int correctByDifficulty[4] = {0};
    int attemptedByDifficulty[4] = {0};
    int timeByDifficulty[4] = {0};
    char* diffNames[] = {"", "⭐ Easy", "⭐⭐ Medium", "⭐⭐⭐ Hard"};
    float diffWeights[] = {0, 1.0, 1.5, 2.0};

    // Shuffle questions
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

    // Exam start message
    printf("\n📝 Exam starting now. You will be shown %d questions.\n", totalQuestions);
    printf("⏱️  You have %d seconds per question.\n", answerTimeout);
    printf("⏳ Overall exam time: %d seconds.\n", overallExamTime);
    printf("💡 Question weights: Easy(x%.1f) Medium(x%.1f) Hard(x%.1f)\n", 
           diffWeights[1], diffWeights[2], diffWeights[3]);
    printf("🚪 Enter 'e' at any time to exit the exam.\n\n");

    char answerBuf[20];

    for (int i = 0; i < totalQuestions; i++) {
        if (examTimeUp) {
            printf("\n⏰ Overall exam time has expired.\n");
            break;
        }

        Question *q = &questions[indices[i]];
        if (q->question[0] == '\0' || q->difficulty < 1 || q->difficulty > 3) {
            printf("📛 Invalid question %d, skipping\n", i+1);
            wrongCount++;
            attempted++;
            continue;
        }

        // Display question
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

        // Start timer for current question
        questionStartTime = time(NULL);
        int gotInput = get_input_with_timeout(answerBuf, sizeof(answerBuf), answerTimeout);
        int answerTime = time(NULL) - questionStartTime;

        totalAnswerTime += answerTime;
        timeByDifficulty[q->difficulty] += answerTime;
        result.responseTimes[i] = answerTime;

        clear_input_buffer();

        if (!gotInput) {
            printf("\n⏰ Time's up for this question! No answer provided.\n");
            wrongCount++;
            attempted++;
            attemptedByDifficulty[q->difficulty]++;
            printf("\n--------------------------\n\n");
            continue;
        }

        if (answerBuf[0] == 'e' || answerBuf[0] == 'E') {
            printf("\n🚪 Exiting exam early...\n");
            break;
        }

        char userAns = toupper(answerBuf[0]);
        if (strchr("ABCD", userAns) == NULL) {
            printf("\n📛 Invalid answer! Treated as wrong.\n");
            wrongCount++;
            attempted++;
            attemptedByDifficulty[q->difficulty]++;
            continue;
        }

        attempted++;
        attemptedByDifficulty[q->difficulty]++;

        if (answerTime < MIN_ANSWER_TIME) {
            printf("\n⚠️  Warning: You answered very quickly (%d seconds).\n", answerTime);
            isCheating = 1;
        }

        if (userAns == q->correct) {
            printf("✅ Correct! (+%.1f points)\n", diffWeights[q->difficulty]);
            weightedScore += diffWeights[q->difficulty];
            correctByDifficulty[q->difficulty]++;
        } else {
            printf("❌ Wrong! Correct answer: %c\n", q->correct);
            wrongCount++;
        }

        printf("\n--------------------------\n\n");
    }

    // Additional cheating detection
    if (attempted > 0 && (totalAnswerTime / attempted) < MIN_ANSWER_TIME) {
        isCheating = 1;
    }

    // Final result summary
    int correctCount = attempted - wrongCount;
    double accuracy = (attempted > 0) ? ((double)correctCount / attempted) * 100 : 0;

    printf("\n***********************************************************************\n");
    printf("*                         📊 DETAILED RESULTS                        *\n");
    printf("***********************************************************************\n");
    printf("| %-25s: %10.2f (Max: %.1f)                   |\n", "🎯 Weighted Score", weightedScore, 
          totalQuestions * diffWeights[3]);
    printf("| %-25s: %10.2f%%                                   |\n", "📈 Overall Accuracy", accuracy);

    printf("\n--------------------------------------------------------\n");
    printf("| %-12s | %-8s | %-8s | %-8s | %-8s |\n", "Difficulty", "Correct", "Attempted", "Accuracy", "Avg Time");
    printf("--------------------------------------------------------\n");

    for (int i = 1; i <= 3; i++) {
        if (attemptedByDifficulty[i] > 0) {
            float diffAccuracy = (float)correctByDifficulty[i] / attemptedByDifficulty[i] * 100;
            float avgTime = (float)timeByDifficulty[i] / attemptedByDifficulty[i];

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

    // Fill result struct
    result.correctAnswers = correctCount;
    result.totalQuestions = attempted;
    result.totalTime = totalAnswerTime;
    result.flagged = isCheating;

    // Send result to server
    if (send(sock, &result, sizeof(ExamResult), 0) < 0) {
        perror("📛 Error sending exam result");
    } else {
        printf("📤 Sent exam result to server\n");
    }
}
