#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <direct.h>
#include <iomanip>
#include <cerrno>
#include <cstring>
#include <limits>

using namespace std;
 
SQLHANDLE envHandle, connHandle, stmtHandle;
string currentUserRole;
wstring stringToWstring(const string& str) {
    wstring wstr(str.begin(), str.end());
    return wstr;
}
string wstring_to_string(const wstring& wstr) {
    return string(wstr.begin(), wstr.end());
}
void showError(SQLHANDLE handle, SQLSMALLINT type) {
    SQLWCHAR state[1024], message[1024];
    if (SQL_SUCCESS == SQLGetDiagRecW(type, handle, 1, state, NULL, message, 1024, NULL)) {
        cout << "SQL Error: " << wstring_to_string(message) << " (State: " << wstring_to_string(state) << ")" << endl;
    }
}
bool connectDB() {
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &envHandle)) return false;
    if (SQL_SUCCESS != SQLSetEnvAttr(envHandle, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0)) return false;
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_DBC, envHandle, &connHandle)) return false;
    wstring connStr = L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=PSILENL060;DATABASE=library_management;Trusted_Connection=Yes;Integrated Security=SSPI;";
    SQLWCHAR retConnStr[1024];
    SQLSMALLINT retConnStrLen;
    SQLRETURN ret = SQLDriverConnectW(connHandle, NULL, (SQLWCHAR*)connStr.c_str(), SQL_NTS, retConnStr, 1024, &retConnStrLen, SQL_DRIVER_NOPROMPT);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        showError(connHandle, SQL_HANDLE_DBC);
        return false;
    }
    cout << "Connected to database: LibDB" << endl;
    return true;
}
void disconnectDB() {
    SQLFreeHandle(SQL_HANDLE_STMT, stmtHandle);
    SQLDisconnect(connHandle);
    SQLFreeHandle(SQL_HANDLE_DBC, connHandle);
    SQLFreeHandle(SQL_HANDLE_ENV, envHandle);
}
bool runQuery(const string& query, bool useTransaction = false) {
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_STMT, connHandle, &stmtHandle)) return false;
    if (useTransaction) {
        SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    }
    wstring wquery = stringToWstring(query);
    SQLRETURN ret = SQLExecDirectW(stmtHandle, (SQLWCHAR*)wquery.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        showError(stmtHandle, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, stmtHandle);
        if (useTransaction) SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
        return false;
    }
    if (useTransaction) {
        SQLEndTran(SQL_HANDLE_DBC, connHandle, SQL_COMMIT);
        SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmtHandle);
    return true;
}
vector<vector<string>> getResults(const string& query) {
    vector<vector<string>> results;
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_STMT, connHandle, &stmtHandle)) return results;
    wstring wquery = stringToWstring(query);
    SQLRETURN ret = SQLExecDirectW(stmtHandle, (SQLWCHAR*)wquery.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        showError(stmtHandle, SQL_HANDLE_STMT);
        SQLFreeHandle(SQL_HANDLE_STMT, stmtHandle);
        return results;
    }
    SQLSMALLINT numCols;
    SQLNumResultCols(stmtHandle, &numCols);
    while (SQLFetch(stmtHandle) == SQL_SUCCESS) {
        vector<string> row;
        for (SQLSMALLINT i = 1; i <= numCols; ++i) {
            SQLWCHAR data[1024];
            SQLLEN dataLen;
            SQLGetData(stmtHandle, i, SQL_C_WCHAR, data, 1024 * sizeof(SQLWCHAR), &dataLen);
            row.push_back(dataLen != SQL_NULL_DATA ? wstring_to_string(wstring(data)) : "NULL");
        }
        results.push_back(row);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmtHandle);
    return results;
}
string escapeCSV(const string& field) {
    string result = field;
    if (field.find(',') != string::npos || field.find('"') != string::npos) {
        string escaped;
        escaped += '"';
        for (char c : field) {
            if (c == '"') escaped += "\"\"";
            else escaped += c;
        }
        escaped += '"';
        return escaped;
    }
    return result;
}
void exportReportsToCSV() {
    char cwd[256];
    if (_getcwd(cwd, sizeof(cwd)) == nullptr) {  // Check if _getcwd succeeded
        cout << "Failed to get current working directory." << endl;
        return;
    }

    string basePath = string(cwd) + "\\";

    auto exportToFile = [&](const string& filename, const vector<vector<string>>& data, const string& header) {
        ofstream file(basePath + filename);
        if (!file.is_open()) {
            cout << "Failed to create " << filename << endl;
            return;
        }
        file << header << "\n";
        for (const auto& row : data) {
            file << row[0] << "," << escapeCSV(row[1]) << "," << row[2] << "\n";
        }
        file.close();
        cout << "Exported " << filename << " to " << basePath << endl;
    };

    auto topBooks = getResults("SELECT b.BookID, b.Title, COUNT(t.TransactionID) as IssueCount FROM dbo.Books b LEFT JOIN dbo.Transactions t ON b.BookID = t.BookID GROUP BY b.BookID, b.Title ORDER BY IssueCount DESC");
    exportToFile("top_issued_books.csv", topBooks, "BookID,Title,IssueCount");

    auto activeMembers = getResults("SELECT m.MemberID, m.Name, COUNT(t.TransactionID) as BooksIssued FROM dbo.Members m LEFT JOIN dbo.Transactions t ON m.MemberID = t.MemberID GROUP BY m.MemberID, m.Name ORDER BY BooksIssued DESC");
    exportToFile("active_members.csv", activeMembers, "MemberID,Name,BooksIssued");

    auto fineSummary = getResults("SELECT m.MemberID, m.Name, SUM(t.FineAmount) as TotalFine FROM dbo.Members m LEFT JOIN dbo.Transactions t ON m.MemberID = t.MemberID GROUP BY m.MemberID, m.Name ORDER BY TotalFine DESC");
    exportToFile("fine_summary.csv", fineSummary, "MemberID,Name,TotalFine");
}

bool login() {
    int roleChoice;
    string username, password, role;

    cout << "Who wants to login?\n1) Admin\n2) User\nChoice: ";
    cin >> roleChoice;
    while (cin.fail() || roleChoice < 1 || roleChoice > 2) {
        cout << "Invalid choice! Enter 1 for Admin or 2 for User: ";
        cin.clear();
        cin.ignore(10000, '\n');  // clear bad input
        cin >> roleChoice;
    }

    role = (roleChoice == 1) ? "Admin" : "User";

    cout << "Enter Username: ";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');  // clear leftover newline
    getline(cin, username);

    cout << "Enter Password: ";
    cin >> password;

    string query = "SELECT Role FROM dbo.Members WHERE Name = '" + username + "' AND Password = '" + password + "' AND Role = '" + role + "'";
    auto res = getResults(query);

    if (res.empty()) {
        cout << "Invalid credentials for " << role << "!" << endl;
        return false;
    }

    currentUserRole = res[0][0];
    cout << "Logged in as " << currentUserRole << endl;
    return true;
}

void showPaginated(const vector<vector<string>>& data, const string& type) {
    if (data.empty()) {
        cout << "No " << type << " found." << endl;
        return;
    }

    const int pageSize = 5;
    int page = 0;
    char choice;

    do {
        system("cls");
        int start = page * pageSize;
        int end = min(start + pageSize, static_cast<int>(data.size()));

        if (type == "Books") {
            cout << left << setw(5) << "ID"
                 << setw(25) << "Title"
                 << setw(20) << "Authors"
                 << setw(12) << "Genre"
                 << setw(15) << "Publisher"
                 << setw(8) << "Ed."
                 << setw(6) << "Year"
                 << setw(8) << "Price"
                 << setw(10) << "Rack"
                 << setw(12) << "Language"
                 << setw(8) << "Avail" << endl;
            cout << string(125, '-') << endl;

            for (int i = start; i < end; ++i) {
                if (data[i].size() < 11) {
                    cout << "Warning: Row " << i << " has incomplete data." << endl;
                    continue;
                }
                cout << left << setw(5) << data[i][0]
                     << setw(25) << data[i][1].substr(0, 24)
                     << setw(20) << data[i][2].substr(0, 19)
                     << setw(12) << data[i][3].substr(0, 11)
                     << setw(15) << data[i][4].substr(0, 14)
                     << setw(8)  << data[i][5]
                     << setw(6)  << data[i][6]
                     << setw(8)  << data[i][7]
                     << setw(10) << data[i][8].substr(0, 8)
                     << setw(12) << data[i][9].substr(0, 10)
                     << setw(8)  << data[i][10]
                     << endl;
            }
        } else if (type == "Members") {
            cout << left << setw(8) << "ID"
                 << setw(30) << "Name"
                 << setw(30) << "Email"
                 << setw(10) << "Type" << endl;
            cout << string(78, '-') << endl;

            for (int i = start; i < end; ++i) {
                cout << left << setw(8) << data[i][0]
                     << setw(30) << data[i][1].substr(0, 29)
                     << setw(30) << data[i][2].substr(0, 29)
                     << setw(10) << data[i][3] << endl;
            }
        } else if (type == "Transactions") {
            cout << left << setw(8) << "ID"
                 << setw(10) << "BookID"
                 << setw(10) << "MemberID"
                 << setw(12) << "IssueDate"
                 << setw(12) << "DueDate"
                 << setw(10) << "Status"
                 << setw(8)  << "Fine" << endl;
            cout << string(78, '-') << endl;

            for (int i = start; i < end; ++i) {
                cout << left << setw(8) << data[i][0]
                     << setw(10) << data[i][1]
                     << setw(10) << data[i][2]
                     << setw(12) << data[i][3]
                     << setw(12) << data[i][4]
                     << setw(10) << data[i][5]
                     << setw(8)  << data[i][6] << endl;
            }
        } else if (type == "TopBooks") {
            cout << left << setw(8) << "BookID"
                 << setw(30) << "Title"
                 << setw(12) << "IssueCount" << endl;
            cout << string(50, '-') << endl;

            for (int i = start; i < end; ++i) {
                cout << left << setw(8) << data[i][0]
                     << setw(30) << data[i][1].substr(0, 29)
                     << setw(12) << data[i][2] << endl;
            }
        } else if (type == "ActiveMembers" || type == "Fines") {
            string label = (type == "ActiveMembers") ? "BooksIssued" : "TotalFine";
            cout << left << setw(10) << "MemberID"
                 << setw(30) << "Name"
                 << setw(15) << label << endl;
            cout << string(55, '-') << endl;

            for (int i = start; i < end; ++i) {
                cout << left << setw(10) << data[i][0]
                     << setw(30) << data[i][1].substr(0, 29)
                     << setw(15) << data[i][2] << endl;
            }
        }

        int totalPages = (data.size() + pageSize - 1) / pageSize;
        cout << "\nPage " << (page + 1) << " of " << totalPages;
        cout << " | [N]ext, [P]revious, [Q]uit: ";
        cin >> choice;
        choice = toupper(choice);
        cin.ignore(10000, '\n');  // clear input buffer after reading choice

        if (choice == 'N' && end < static_cast<int>(data.size()))
            ++page;
        else if (choice == 'P' && page > 0)
            --page;

    } while (choice != 'Q');

    cout << "Exiting view. Press Enter to continue...";
    cin.ignore();
    cin.get();
}
struct Config {
    double fineRate;
    int maxBooksPerMember;
    int reservationDurationDays;
};

Config getConfig() {
    Config config = {1.00, 5, 7};
    auto res = getResults("SELECT FineRate, MaxBooksPerMember, ReservationDurationDays FROM dbo.Config WHERE ConfigID = 1");
    if (!res.empty()) {
        try {
            config.fineRate = stod(res[0][0]);
            config.maxBooksPerMember = stoi(res[0][1]);
            config.reservationDurationDays = stoi(res[0][2]);
        } catch (const std::exception& e) {
            cout << "Error parsing config values: " << e.what() << endl;
            // fallback to defaults already set
        }
    }
    return config;
}

void addBook() {
    string title, authors, genre, publisher, isbn, edition, rackLocation, language, availability;
    int publishedYear = 0;
    double price = 0.0;

    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // clear input buffer
    cout << "Enter Title: "; getline(cin, title);
    cout << "Enter Authors: "; getline(cin, authors);
    cout << "Enter Genre: "; getline(cin, genre);
    cout << "Enter Publisher: "; getline(cin, publisher);
    cout << "Enter ISBN: "; getline(cin, isbn);
    cout << "Enter Edition: "; getline(cin, edition);
    cout << "Enter Published Year: "; cin >> publishedYear;
    cout << "Enter Price: "; cin >> price;
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    cout << "Enter Rack Location: "; getline(cin, rackLocation);
    cout << "Enter Language: "; getline(cin, language);
    cout << "Is Available (Yes/No): "; getline(cin, availability);

    if (title.empty() || authors.empty() || isbn.empty()) {
        cout << "Title, Authors, and ISBN are required!" << endl;
        return;
    }

    // Normalize availability to lowercase for comparison
    string availabilityLower = availability;
    transform(availabilityLower.begin(), availabilityLower.end(), availabilityLower.begin(), ::tolower);
    if (availabilityLower != "yes" && availabilityLower != "no") {
        cout << "Availability must be 'Yes' or 'No'." << endl;
        return;
    }

    auto escape = [](string& str) {
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '\'') str.insert(i++, "'");
        }
    };

    escape(title);
    escape(authors);
    escape(genre);
    escape(publisher);
    escape(isbn);
    escape(edition);
    escape(rackLocation);
    escape(language);
    escape(availability);

    auto res = getResults("SELECT ISBN FROM dbo.Books WHERE ISBN = '" + isbn + "'");
    if (!res.empty()) {
        cout << "ISBN already exists!" << endl;
        return;
    }

    string query = "INSERT INTO dbo.Books "
                   "(Title, Authors, Genre, Publisher, ISBN, Edition, PublishedYear, Price, RackLocation, Language, Availability) "
                   "VALUES ('" + title + "', '" + authors + "', '" + genre + "', '" + publisher + "', '" + isbn + "', '" + edition + "', "
                   + to_string(publishedYear) + ", " + to_string(price) + ", '" + rackLocation + "', '" + language + "', '" + availability + "')";

    if (runQuery(query)) {
        cout << "Book added!" << endl;
    } else {
        cout << "Failed to add book." << endl;
    }
}

void updateBook() {
    string bookID;
    cout << "Enter BookID: ";
    cin >> bookID;

    if (!all_of(bookID.begin(), bookID.end(), ::isdigit)) {
        cout << "BookID must be numeric!" << endl;
        return;
    }

    auto res = getResults("SELECT BookID FROM dbo.Books WHERE BookID = '" + bookID + "'");
    if (res.empty()) {
        cout << "Book not found!" << endl;
        return;
    }

    string title, authors, isbn;
    cout << "Enter new Title (blank to skip): ";
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    getline(cin, title);
    cout << "Enter new Authors (blank to skip): ";
    getline(cin, authors);
    cout << "Enter new ISBN (blank to skip): ";
    getline(cin, isbn);

    string query = "UPDATE dbo.Books SET ";
    bool hasUpdate = false;

    auto escape = [](string& str) {
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '\'') str.insert(i++, "'");
        }
    };

    if (!title.empty()) {
        escape(title);
        query += "Title = '" + title + "'";
        hasUpdate = true;
    }
    if (!authors.empty()) {
        escape(authors);
        query += (hasUpdate ? ", " : "") + string("Authors = '") + authors + "'";
        hasUpdate = true;
    }
    if (!isbn.empty()) {
        escape(isbn);
        auto res = getResults("SELECT ISBN FROM dbo.Books WHERE ISBN = '" + isbn + "' AND BookID != '" + bookID + "'");
        if (!res.empty()) {
            cout << "ISBN already exists!" << endl;
            return;
        }
        query += (hasUpdate ? ", " : "") + string("ISBN = '") + isbn + "'";
        hasUpdate = true;
    }

    if (!hasUpdate) {
        cout << "No changes provided!" << endl;
        return;
    }

    query += " WHERE BookID = '" + bookID + "'";

    if (runQuery(query)) {
        cout << "Book updated!" << endl;
    } else {
        cout << "Failed to update book." << endl;
    }
}

void deleteBook() {
    string bookID;
    cout << "Enter BookID: ";
    cin >> bookID;

    if (!all_of(bookID.begin(), bookID.end(), ::isdigit)) {
        cout << "BookID must be numeric!" << endl;
        return;
    }

    auto res = getResults("SELECT BookID FROM dbo.Books WHERE BookID = '" + bookID + "'");
    if (res.empty()) {
        cout << "Book not found!" << endl;
        return;
    }

    string query = "DELETE FROM dbo.Books WHERE BookID = '" + bookID + "'";
    if (runQuery(query)) {
        cout << "Book deleted!" << endl;
    } else {
        cout << "Failed to delete book." << endl;
    }
}

void viewBooks() {
    auto res = getResults("SELECT DB_NAME() AS DatabaseName");
    if (!res.empty()) {
        cout << "Connected to database: " << res[0][0] << endl;
    } else {
        cout << "Failed to retrieve database name." << endl;
    }

    res = getResults(
        "SELECT BookID, Title, Authors, Genre, Publisher, Edition, PublishedYear, Price, RackLocation, Language, Availability "
        "FROM dbo.Books ORDER BY BookID"
    );

    cout << "Fetched " << res.size() << " books from dbo.Books" << endl;
    showPaginated(res, "Books");
}

void searchBooks() {
    string value;
    cout << "Enter Title or Author to search: ";
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    getline(cin, value);

    // Escape single quotes for SQL query safety
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '\'') value.insert(i++, "'");
    }

    string query = "SELECT BookID, Title, Authors, Availability FROM dbo.Books WHERE Title LIKE '%" + value + "%' OR Authors LIKE '%" + value + "%'";
    auto res = getResults(query);
    showPaginated(res, "Books");
}

vector<vector<string>> getResults(const string &query);

vector<string> parseCSVLine(const string &line) {
    vector<string> result;
    string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            result.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    result.push_back(field);
    return result;
}

 
void bulkImportBooks() {
    char cwd[256];
    if (_getcwd(cwd, sizeof(cwd)) == nullptr) {
        cout << "Error retrieving current directory: " << strerror(errno) << endl;
        return;
    }

    string filename = "books.csv";
    int attempts = 3;
    ifstream file;

    while (attempts > 0) {
        file.open(filename);
        if (!file.is_open()) {
            cout << "Failed to open " << filename << " in " << cwd << ": " << strerror(errno) << endl;
            attempts--;

            if (attempts == 0) {
                cout << "Trying fallback path..." << endl;
                filename = "C:\\task_4\\books.csv";
                file.open(filename);
                if (!file.is_open()) {
                    cout << "Failed to open fallback file " << filename << ": " << strerror(errno) << endl;
                    cout << "Please enter the full path to books.csv (or 'q' to quit): ";
                    cin >> filename;
                    if (filename == "q" || filename == "Q") {
                        cout << "Bulk import aborted." << endl;
                        return;
                    }
                    continue;
                }
            } else {
                cout << "Attempts remaining: " << attempts
                     << ". Please ensure books.csv exists in " << cwd
                     << " or enter the full path (or 'q' to quit): ";
                cin >> filename;
                if (filename == "q" || filename == "Q") {
                    cout << "Bulk import aborted." << endl;
                    return;
                }
                continue;
            }
        } else {
            // File opened successfully, break the loop
            break;
        }
    }

    if (!file.is_open()) {
        cout << "Unable to open books.csv after multiple attempts." << endl;
        return;
    }

    cout << "Reading books from " << filename << endl;
    string line;
    int lineNum = 0;
    int booksAdded = 0;

    auto trim = [](string &s) {
        const char* whitespace = " \t\n\r\"";
        size_t start = s.find_first_not_of(whitespace);
        size_t end = s.find_last_not_of(whitespace);
        if (start == string::npos || end == string::npos) {
            s.clear();
        } else {
            s = s.substr(start, end - start + 1);
        }
    };

    auto escapeQuotes = [](string &s) {
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '\'') s.insert(i++, "'");
        }
    };

    while (getline(file, line)) {
        lineNum++;
        if (line.empty() || all_of(line.begin(), line.end(), ::isspace)) {
            cout << "Skipping empty line " << lineNum << endl;
            continue;
        }

        vector<string> fields = parseCSVLine(line);
        if (fields.size() < 11) {
            cout << "Invalid format at line " << lineNum << ": " << line << endl;
            continue;
        }

        string title = fields[0];
        string author = fields[1];
        string category = fields[2];
        string publisher = fields[3];
        string isbn = fields[4];
        string edition = fields[5];

        int year = 0;
        try {
            year = stoi(fields[6]);
        } catch (...) {
            cout << "Invalid year at line " << lineNum << ": " << fields[6] << endl;
            continue;
        }

        float price = 0.0f;
        try {
            price = stof(fields[7]);
        } catch (...) {
            cout << "Invalid price at line " << lineNum << ": " << fields[7] << endl;
            continue;
        }

        string shelf = fields[8];
        string language = fields[9];
        string available = fields[10];

        trim(title);
        trim(author);
        trim(category);
        trim(publisher);
        trim(isbn);
        trim(edition);
        trim(shelf);
        trim(language);
        trim(available);

        if (title.empty() || author.empty() || isbn.empty()) {
            cout << "Invalid format at line " << lineNum << ": missing required fields" << endl;
            continue;
        }

        // Check if ISBN already exists
        auto res = getResults("SELECT ISBN FROM dbo.Books WHERE ISBN = '" + isbn + "'");
        if (!res.empty()) {
            cout << "ISBN exists at line " << lineNum << ": " << isbn << endl;
            continue;
        }

        escapeQuotes(title);
        escapeQuotes(author);
        escapeQuotes(category);
        escapeQuotes(publisher);
        escapeQuotes(isbn);
        escapeQuotes(edition);
        escapeQuotes(shelf);
        escapeQuotes(language);
        escapeQuotes(available);

        string query = "INSERT INTO dbo.Books (Title, Author, Category, Publisher, ISBN, Edition, Year, Price, Shelf, Language, Available) VALUES (";
        query += "'" + title + "', '" + author + "', '" + category + "', '" + publisher + "', '" + isbn + "', '" + edition + "', ";
        query += to_string(year) + ", " + to_string(price) + ", '" + shelf + "', '" + language + "', '" + available + "')";

        if (runQuery(query)) {
            cout << "Added: " << title << " (Line " << lineNum << ")" << endl;
            booksAdded++;
        } else {
            cout << "Failed to add at line " << lineNum << ": " << title << endl;
        }
    }

    file.close();
    cout << "Bulk import completed. Added " << booksAdded << " books." << endl;
}

void booksMenu() {
    int choice;
    do {
        cout << "\n=== Books Management ===\n";
        cout << "1. Add Book\n";
        cout << "2. Update Book\n";
        cout << "3. Delete Book\n";
        cout << "4. View Books\n";
        cout << "5. Search Books\n";
        cout << "6. Bulk Import Books\n";
        cout << "7. Back to Main Menu\n";
        cout << "Enter your choice (1-7): ";
        cin >> choice;

        while (cin.fail() || choice < 1 || choice > 7) {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cout << "Invalid choice! Please enter a number between 1 and 7: ";
            cin >> choice;
        }

        switch (choice) {
            case 1: addBook(); break;
            case 2: updateBook(); break;
            case 3: deleteBook(); break;
            case 4: viewBooks(); break;
            case 5: searchBooks(); break;
            case 6: bulkImportBooks(); break;
            case 7: cout << "Returning to main menu...\n"; break;
        }
    } while (choice != 7);
}

 
void addMember() {
    string name, email, type, role, pass;

    cin.ignore(numeric_limits<streamsize>::max(), '\n');  // Clear leftover input
    cout << "Enter Name: ";
    getline(cin, name);
    cout << "Enter Email: ";
    getline(cin, email);
    cout << "Enter Type (Regular/Premium): ";
    getline(cin, type);
    cout << "Enter Role (Admin/User): ";
    getline(cin, role);
    cout << "Enter Password: ";
    getline(cin, pass);

    // Normalize input
    transform(type.begin(), type.end(), type.begin(), ::tolower);
    transform(role.begin(), role.end(), role.begin(), ::tolower);

    if (type != "regular" && type != "premium") {
        cout << "Invalid Type! Use 'Regular' or 'Premium'.\n";
        return;
    }
    if (role != "admin" && role != "user") {
        cout << "Invalid Role! Use 'Admin' or 'User'.\n";
        return;
    }
    if (name.empty() || email.empty() || pass.empty()) {
        cout << "Name, Email, and Password are required!\n";
        return;
    }

    // Escape single quotes for SQL safety
    auto escape = [](string &str) {
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '\'') str.insert(i++, "'");
        }
    };

    escape(name);
    escape(email);
    escape(type);
    escape(role);
    escape(pass);

    auto res = getResults("SELECT Email FROM dbo.Members WHERE Email = '" + email + "'");
    if (!res.empty()) {
        cout << "Email already exists!\n";
        return;
    }

    string query = "INSERT INTO dbo.Members (Name, Email, MembershipType, Role, Password) VALUES ('" +
                   name + "', '" + email + "', '" + type + "', '" + role + "', '" + pass + "')";

    if (runQuery(query)) {
        cout << "Member added successfully!\n";
    } else {
        cout << "Failed to add member.\n";
    }
}
 
 
void updateMember() {
    string memberID;
    cout << "Enter MemberID: ";
    cin >> memberID;

    if (!all_of(memberID.begin(), memberID.end(), ::isdigit)) {
        cout << "MemberID must be numeric!" << endl;
        return;
    }

    auto res = getResults("SELECT MemberID FROM dbo.Members WHERE MemberID = '" + memberID + "'");
    if (res.empty()) {
        cout << "Member not found!" << endl;
        return;
    }

    string name, email, type;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');  // Clear input buffer

    cout << "Enter new Name (leave blank to skip): ";
    getline(cin, name);
    cout << "Enter new Email (leave blank to skip): ";
    getline(cin, email);
    cout << "Enter new Type (Regular/Premium, leave blank to skip): ";
    getline(cin, type);

    // Helper to escape single quotes
    auto escape = [](string &s) {
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '\'') s.insert(i++, "'");
        }
    };

    string query = "UPDATE dbo.Members SET ";
    bool hasUpdate = false;

    if (!name.empty()) {
        escape(name);
        query += "Name = '" + name + "'";
        hasUpdate = true;
    }

    if (!email.empty()) {
        escape(email);
        auto emailRes = getResults("SELECT Email FROM dbo.Members WHERE Email = '" + email + "' AND MemberID != '" + memberID + "'");
        if (!emailRes.empty()) {
            cout << "Email already exists!" << endl;
            return;
        }
        if (hasUpdate) query += ", ";
        query += "Email = '" + email + "'";
        hasUpdate = true;
    }

    if (!type.empty()) {
        // Normalize type (case-insensitive)
        transform(type.begin(), type.end(), type.begin(), ::tolower);
        if (type != "regular" && type != "premium") {
            cout << "Invalid Type! Use 'Regular' or 'Premium'." << endl;
            return;
        }
        type[0] = toupper(type[0]);  // Capitalize for DB consistency

        if (hasUpdate) query += ", ";
        query += "MembershipType = '" + type + "'";
        hasUpdate = true;
    }

    if (!hasUpdate) {
        cout << "No changes provided!" << endl;
        return;
    }

    query += " WHERE MemberID = '" + memberID + "'";

    if (runQuery(query)) {
        cout << "Member updated successfully!" << endl;
    } else {
        cout << "Failed to update member." << endl;
    }
}
void deleteMember() {
    string memberID;
    cout << "Enter MemberID: ";
    cin >> memberID;

    if (!all_of(memberID.begin(), memberID.end(), ::isdigit)) {
        cout << "MemberID must be numeric!" << endl;
        return;
    }

    auto res = getResults("SELECT MemberID FROM dbo.Members WHERE MemberID = '" + memberID + "'");
    if (res.empty()) {
        cout << "Member not found!" << endl;
        return;
    }

    string query = "DELETE FROM dbo.Members WHERE MemberID = '" + memberID + "'";
    if (runQuery(query)) {
        cout << "Member deleted successfully!" << endl;
    } else {
        cout << "Failed to delete member." << endl;
    }
}

void viewMembers() {
    auto res = getResults("SELECT MemberID, Name, Email, MembershipType FROM dbo.Members ORDER BY MemberID");
    if (res.empty()) {
        cout << "No members found." << endl;
        return;
    }
    showPaginated(res, "Members");
}

void searchMembers() {
    string value;
    cout << "Enter Name or Email to search: ";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    getline(cin, value);

    // Escape single quotes to prevent SQL injection or errors
    for (size_t i = 0; i < value.length(); ++i) {
        if (value[i] == '\'') value.insert(i++, "'");
    }

    string query = "SELECT MemberID, Name, Email, MembershipType FROM dbo.Members "
                   "WHERE Name LIKE '%" + value + "%' OR Email LIKE '%" + value + "%'";
    auto res = getResults(query);

    if (res.empty()) {
        cout << "No matching members found." << endl;
        return;
    }

    showPaginated(res, "Members");
}

void membersMenu() {
    int choice;
    do {
        cout << "\n===== Members Management =====\n";
        cout << "1. Add Member\n";
        cout << "2. Update Member\n";
        cout << "3. Delete Member\n";
        cout << "4. View Members\n";
        cout << "5. Search Members\n";
        cout << "6. Back to Main Menu\n";
        cout << "Choice: ";
        cin >> choice;

        while (cin.fail() || choice < 1 || choice > 6) {
            cout << "Invalid choice! Try again: ";
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            cin >> choice;
        }

        switch (choice) {
            case 1: addMember(); break;
            case 2: updateMember(); break;
            case 3: deleteMember(); break;
            case 4: viewMembers(); break;
            case 5: searchMembers(); break;
            case 6: cout << "Returning to main menu...\n"; break;
        }
    } while (choice != 6);
}

 
void issueBook() {
    string bookID, memberID;
    cout << "Enter BookID: ";
    cin >> bookID;
    cout << "Enter MemberID: ";
    cin >> memberID;

    if (!all_of(bookID.begin(), bookID.end(), ::isdigit) || !all_of(memberID.begin(), memberID.end(), ::isdigit)) {
        cout << "BookID and MemberID must be numeric!" << endl;
        return;
    }

    auto bookRes = getResults("SELECT Availability FROM dbo.Books WITH (ROWLOCK, UPDLOCK) WHERE BookID = '" + bookID + "'");
    auto memberRes = getResults("SELECT MemberID FROM dbo.Members WHERE MemberID = '" + memberID + "'");

    if (bookRes.empty() || memberRes.empty()) {
        cout << "Book or Member not found!" << endl;
        return;
    }

    if (bookRes[0][0] == "No") {
        cout << "Book not available!" << endl;
        return;
    }

    Config config = getConfig();
    auto issuedCount = getResults("SELECT COUNT(*) FROM dbo.Transactions WHERE MemberID = '" + memberID + "' AND Status = 'Issued'");

    if (!issuedCount.empty() && stoi(issuedCount[0][0]) >= config.maxBooksPerMember) {
        cout << "Member has reached max limit (" << config.maxBooksPerMember << ")!" << endl;
        return;
    }

    string issueQuery = "INSERT INTO dbo.Transactions (BookID, MemberID, IssueDate, DueDate, Status) "
                        "VALUES ('" + bookID + "', '" + memberID + "', GETDATE(), "
                        "DATEADD(day, " + to_string(config.reservationDurationDays) + ", GETDATE()), 'Issued')";

    string updateBookQuery = "UPDATE dbo.Books SET Availability = 'No' WHERE BookID = '" + bookID + "'";

    bool success = true;
    SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

    if (!runQuery(issueQuery)) success = false;
    if (success && !runQuery(updateBookQuery)) success = false;

    if (success) {
        SQLEndTran(SQL_HANDLE_DBC, connHandle, SQL_COMMIT);
        cout << "Book issued successfully!" << endl;
    } else {
        SQLEndTran(SQL_HANDLE_DBC, connHandle, SQL_ROLLBACK);
        cout << "Failed to issue book." << endl;
    }

    SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
}
void reserveBook() {
    string bookID, memberID;
    cout << "Enter BookID: ";
    cin >> bookID;
    cout << "Enter MemberID: ";
    cin >> memberID;

    if (!all_of(bookID.begin(), bookID.end(), ::isdigit) || !all_of(memberID.begin(), memberID.end(), ::isdigit)) {
        cout << "BookID and MemberID must be numeric!" << endl;
        return;
    }

    auto bookRes = getResults("SELECT Availability FROM dbo.Books WHERE BookID = '" + bookID + "'");
    auto memberRes = getResults("SELECT MemberID FROM dbo.Members WHERE MemberID = '" + memberID + "'");

    if (bookRes.empty() || memberRes.empty()) {
        cout << "Book or Member not found!" << endl;
        return;
    }

    if (bookRes[0][0] == "Yes") {
        cout << "Book is available — consider issuing it instead!" << endl;
        return;
    }

    Config config = getConfig();

    string reserveQuery = "INSERT INTO dbo.Transactions (BookID, MemberID, IssueDate, DueDate, Status) "
                          "VALUES ('" + bookID + "', '" + memberID + "', GETDATE(), "
                          "DATEADD(day, " + to_string(config.reservationDurationDays) + ", GETDATE()), 'Reserved')";

    if (runQuery(reserveQuery)) {
        cout << "Book reserved successfully!" << endl;
    } else {
        cout << "Failed to reserve book." << endl;
    }
}
void returnBook() {
    string transactionID;
    cout << "Enter TransactionID: ";
    cin >> transactionID;

    if (!all_of(transactionID.begin(), transactionID.end(), ::isdigit)) {
        cout << "TransactionID must be numeric!" << endl;
        return;
    }

    auto res = getResults("SELECT BookID FROM dbo.Transactions WHERE TransactionID = '" + transactionID + "' AND Status = 'Issued'");
    if (res.empty()) {
        cout << "Transaction not found or already returned!" << endl;
        return;
    }

    string bookID = res[0][0];
    Config config = getConfig();

    string updateTransaction = "UPDATE dbo.Transactions SET "
                               "Status = 'Returned', "
                               "ReturnDate = GETDATE(), "
                               "FineAmount = CASE WHEN GETDATE() > DueDate "
                               "THEN DATEDIFF(day, DueDate, GETDATE()) * " + to_string(config.fineRate) + 
                               " ELSE 0 END "
                               "WHERE TransactionID = '" + transactionID + "'";

    string updateBook = "UPDATE dbo.Books SET Availability = 'Yes' WHERE BookID = '" + bookID + "'";

    bool success = true;
    SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

    if (!runQuery(updateTransaction)) success = false;
    if (success && !runQuery(updateBook)) success = false;

    if (success) {
        SQLEndTran(SQL_HANDLE_DBC, connHandle, SQL_COMMIT);
        cout << "Book returned successfully!" << endl;
    } else {
        SQLEndTran(SQL_HANDLE_DBC, connHandle, SQL_ROLLBACK);
        cout << "Failed to return book." << endl;
    }

    SQLSetConnectAttr(connHandle, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
}

 


 
void viewHistory() {
    string memberID;
    cout << "Enter MemberID: ";
    cin >> memberID;

    if (!all_of(memberID.begin(), memberID.end(), ::isdigit)) {
        cout << "MemberID must be numeric!" << endl;
        return;
    }

    string query =
        "SELECT TransactionID, BookID, MemberID, IssueDate, DueDate, Status, "
        "ISNULL(FineAmount, 0) AS FineAmount "
        "FROM dbo.Transactions "
        "WHERE MemberID = '" + memberID + "' "
        "ORDER BY IssueDate DESC";

    auto res = getResults(query);

    cout << "Transactions found: " << res.size() << endl;
    if (res.empty()) {
        cout << "No transaction history found for MemberID " << memberID << endl;
        return;
    }

    showPaginated(res, "Transaction History");
}

void transactionsMenu() {
    int choice;
    do {
        cout << "\nTransactions\n";
        cout << "1. Issue Book\n2. Return Book\n3. Reserve Book\n4. View History\n5. Back\nChoice: ";
        cin >> choice;
        while (cin.fail() || choice < 1 || choice > 5) {
            cout << "Invalid choice! Try again: ";
            cin.clear();
            cin.ignore(10000, '\n');
            cin >> choice;
        }
        switch (choice) {
            case 1: issueBook(); break;
            case 2: returnBook(); break;
            case 3: reserveBook(); break;
            case 4: viewHistory(); break;
            case 5: cout << "Returning to main menu..." << endl; break;
        }
    } while (choice != 5);
}
 
void topIssuedBooks() {
    auto res = getResults("SELECT b.BookID, b.Title, COUNT(t.TransactionID) as IssueCount FROM dbo.Books b LEFT JOIN dbo.Transactions t ON b.BookID = t.BookID GROUP BY b.BookID, b.Title ORDER BY IssueCount DESC");
    showPaginated(res, "TopBooks");
}
 
void activeMembers() {
    auto res = getResults("SELECT m.MemberID, m.Name, COUNT(t.TransactionID) as BooksIssued FROM dbo.Members m LEFT JOIN dbo.Transactions t ON m.MemberID = t.MemberID GROUP BY m.MemberID, m.Name ORDER BY BooksIssued DESC");
    showPaginated(res, "ActiveMembers");
}
 
void fineSummary() {
    auto res = getResults("SELECT m.MemberID, m.Name, SUM(t.FineAmount) as TotalFine FROM dbo.Members m LEFT JOIN dbo.Transactions t ON m.MemberID = t.MemberID GROUP BY m.MemberID, m.Name ORDER BY TotalFine DESC");
    showPaginated(res, "Fines");
}
 
void reportsMenu() {
    int choice;
    do {
        cout << "\nReports\n";
        cout << "1. Top Issued Books\n2. Active Members\n3. Fine Summary\n4. Export Reports to CSV\n5. Back\nChoice: ";
        cin >> choice;
        while (cin.fail() || choice < 1 || choice > 5) {
            cout << "Invalid choice! Try again: ";
            cin.clear();
            cin.ignore(10000, '\n');
            cin >> choice;
        }
        switch (choice) {
            case 1: topIssuedBooks(); break;
            case 2: activeMembers(); break;
            case 3: fineSummary(); break;
            case 4: exportReportsToCSV(); break;
            case 5: cout << "Returning to main menu..." << endl; break;
        }
    } while (choice != 5);
}
 
int main() {
    if (!connectDB()) {
        cout << "Failed to connect to database!" << endl;
        return 1;
    }
    char cwd[256];
    _getcwd(cwd, sizeof(cwd));
    int attempts = 3;
    while (attempts > 0 && !login()) {
        attempts--;
        cout << "Attempts remaining: " << attempts << endl;
    }
    if (attempts == 0) {
        cout << "Too many failed attempts. Exiting..." << endl;
        disconnectDB();
        return 1;
    }
    int choice;
    do {
        cout << "\n********** Library Management **********\n";
        if (currentUserRole == "Admin") {
            cout << "1. Books Management\n"
                 << "2. Members Management\n"
                 << "3. Transactions\n"
                 << "4. Reports\n"
                 << "5. Exit\n"
                 << "Choice: ";
        } else {
            cout << "1. View Books\n"
                 << "2. Issue Book\n"
                 << "3. Return Book\n"
                 << "4. Exit\n"
                 << "Choice: ";
        }
        cin >> choice;
        while (cin.fail() || (currentUserRole == "Admin" ? (choice < 1 || choice > 5) : (choice < 1 || choice > 4))) {
            cout << "Invalid choice! Try again: ";
            cin.clear();
            cin.ignore(10000, '\n');
            cin >> choice;
        }
        if (currentUserRole == "Admin") {
            switch (choice) {
                case 1: booksMenu(); break;
                case 2: membersMenu(); break;
                case 3: transactionsMenu(); break;
                case 4: reportsMenu(); break;
                case 5: cout << "Exiting..." << endl; break;
            }
        } else {
            switch (choice) {
                case 1: viewBooks(); break;  
                case 2: issueBook(); break;
                case 3: returnBook(); break;
                case 4: cout << "Exiting..." << endl; break;
            }
        }
 
    } while (choice != (currentUserRole == "Admin" ? 5 : 4));
    disconnectDB();
    return 0;
}