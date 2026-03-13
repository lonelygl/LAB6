

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <unistd.h>
#include <pwd.h>
#include <termios.h>
#include <libpq-fe.h>

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define GRAY    "\033[90m"

static PGconn*      g_conn     = nullptr;
static std::string  g_db_name;
static std::string  g_app_user;
static std::string  g_role;
static std::string  g_os_user;

static std::string get_os_user() {
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_name : "postgres";
}

static std::string input(const std::string& prompt) {
    std::cout << CYAN << prompt << RESET;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

static std::string input_password(const std::string& prompt) {
    std::cout << CYAN << prompt << RESET;
    std::cout.flush();

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::string s;
    std::getline(std::cin, s);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
    return s;
}

static std::string input_confirm_password() {
    while (true) {
        std::string p1 = input_password("Password (exactly 8 chars): ");
        std::string p2 = input_password("Confirm password: ");
        if (p1 == p2) return p1;
        std::cout << RED << "Passwords do not match. Try again.\n" << RESET;
    }
}

static int input_int(const std::string& prompt) {
    while (true) {
        std::string s = input(prompt);
        try { return std::stoi(s); }
        catch (...) { std::cout << RED << "Enter a valid number.\n" << RESET; }
    }
}

static void wait_enter() {
    std::cout << GRAY << "\nPress Enter to continue..." << RESET;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static void clear_screen() {
    std::cout << "\033[2J\033[1;1H";
}

static std::string pg_error(PGconn* c) {
    std::string msg = PQerrorMessage(c);

    auto pos = msg.find("ERROR:");
    if (pos != std::string::npos) {
        std::string tail = msg.substr(pos + 6);

        auto start = tail.find_first_not_of(" \t");
        if (start != std::string::npos) tail = tail.substr(start);

        auto nl = tail.find('\n');
        if (nl != std::string::npos) tail = tail.substr(0, nl);
        return tail;
    }

    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
    return msg;
}

static std::string res_error(PGresult* r) {
    std::string msg = PQresultErrorMessage(r);
    auto pos = msg.find("ERROR:");
    if (pos != std::string::npos) {
        std::string tail = msg.substr(pos + 6);
        auto start = tail.find_first_not_of(" \t");
        if (start != std::string::npos) tail = tail.substr(start);
        auto nl = tail.find('\n');
        if (nl != std::string::npos) tail = tail.substr(0, nl);
        return tail;
    }
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
    return msg;
}

static PGconn* pg_connect(const std::string& dbname,
                           const std::string& user,
                           const std::string& password = "") {
    std::string connstr =
        "host=localhost port=5432 dbname=" + dbname +
        " user=" + user;
    if (!password.empty())
        connstr += " password=" + password;
    PGconn* c = PQconnectdb(connstr.c_str());
    return c;
}

static bool run_setup_sql(const std::string& dbname) {

    std::string sql_path = "./setup.sql";

    std::string cmd = "psql -U " + g_os_user +
                      " -d " + dbname +
                      " -f " + sql_path +
                      " > /tmp/ufc_setup.log 2>&1";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cout << RED << "setup.sql failed. See /tmp/ufc_setup.log\n" << RESET;

        std::system("cat /tmp/ufc_setup.log");
        return false;
    }
    return true;
}

static std::vector<std::string> list_databases() {
    std::vector<std::string> dbs;
    PGconn* c = pg_connect("postgres", g_os_user);
    if (PQstatus(c) != CONNECTION_OK) { PQfinish(c); return dbs; }
    PGresult* r = PQexec(c,
        "SELECT datname FROM pg_database WHERE datistemplate=false ORDER BY datname");
    if (PQresultStatus(r) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(r); i++)
            dbs.push_back(PQgetvalue(r, i, 0));
    }
    PQclear(r);
    PQfinish(c);
    return dbs;
}

static bool create_database(const std::string& dbname) {
    if (!g_role.empty() && g_role != "admin") {
        std::cout << RED << "ERROR: permission denied to create database — admin role required\n" << RESET;
        return false;
    }
    PGconn* c = pg_connect("postgres", g_os_user);
    if (PQstatus(c) != CONNECTION_OK) {
        std::cout << RED << "Cannot connect as superuser: " << pg_error(c) << "\n" << RESET;
        PQfinish(c); return false;
    }

    const char* params[1] = { dbname.c_str() };
    PGresult* r = PQexecParams(c,
        "SELECT 1 FROM pg_database WHERE datname=$1", 1, nullptr, params, nullptr, nullptr, 0);
    bool exists = (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0);
    PQclear(r);
    if (exists) {
        std::cout << RED << "Database '" << dbname << "' already exists.\n" << RESET;
        PQfinish(c); return false;
    }

    std::string sql = "CREATE DATABASE \"" + dbname + "\"";
    r = PQexec(c, sql.c_str());
    if (PQresultStatus(r) != PGRES_COMMAND_OK) {
        std::cout << RED << "CREATE DATABASE failed: " << res_error(r) << "\n" << RESET;
        PQclear(r); PQfinish(c); return false;
    }
    PQclear(r);
    PQfinish(c);

    std::cout << CYAN << "Running setup.sql on '" << dbname << "'...\n" << RESET;
    return run_setup_sql(dbname);
}

static bool drop_database(const std::string& dbname) {
    if (g_role != "admin") {
        std::cout << RED << "ERROR: permission denied to drop database — admin role required\n" << RESET;
        return false;
    }

    if (g_conn) { PQfinish(g_conn); g_conn = nullptr; }

    PGconn* c = pg_connect("postgres", g_os_user);
    if (PQstatus(c) != CONNECTION_OK) {
        std::cout << RED << "Cannot connect as superuser: " << pg_error(c) << "\n" << RESET;
        PQfinish(c); return false;
    }

    const char* params[1] = { dbname.c_str() };
    PGresult* r = PQexecParams(c,
        "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
        "WHERE datname=$1 AND pid<>pg_backend_pid()",
        1, nullptr, params, nullptr, nullptr, 0);
    PQclear(r);

    std::string sql = "DROP DATABASE IF EXISTS \"" + dbname + "\"";
    r = PQexec(c, sql.c_str());
    bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) std::cout << RED << "DROP DATABASE failed: " << res_error(r) << "\n" << RESET;
    PQclear(r);
    PQfinish(c);
    return ok;
}

static bool app_login(const std::string& dbname,
                      const std::string& username,
                      const std::string& password) {

    PGconn* c = pg_connect(dbname, g_os_user);
    if (PQstatus(c) != CONNECTION_OK) {
        std::cout << RED << "Cannot connect to database '" << dbname << "': " << pg_error(c) << "\n" << RESET;
        PQfinish(c); return false;
    }
    const char* params[2] = { username.c_str(), password.c_str() };
    PGresult* r = PQexecParams(c,
        "SELECT fn_app_login($1,$2)", 2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
        std::cout << RED << "Login failed: " << res_error(r) << "\n" << RESET;
        PQclear(r); PQfinish(c); return false;
    }
    std::string role = PQgetvalue(r, 0, 0);
    PQclear(r);
    PQfinish(c);

    std::string pg_user = (role == "admin") ? "ufc_admin" : "ufc_guest";
    std::string pg_pass = (role == "admin") ? "admin123"  : "guest123";
    PGconn* conn = pg_connect(dbname, pg_user, pg_pass);
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cout << RED << "Role connection failed: " << pg_error(conn) << "\n" << RESET;
        PQfinish(conn); return false;
    }
    if (g_conn) PQfinish(g_conn);
    g_conn     = conn;
    g_db_name  = dbname;
    g_app_user = username;
    g_role     = role;
    return true;
}

static bool app_register(const std::string& dbname,
                          const std::string& username,
                          const std::string& password,
                          const std::string& role,
                          const std::string& admin_code) {
    PGconn* c = pg_connect(dbname, g_os_user);
    if (PQstatus(c) != CONNECTION_OK) {
        std::cout << RED << pg_error(c) << "\n" << RESET;
        PQfinish(c); return false;
    }
    const char* params[4] = {
        username.c_str(), password.c_str(),
        role.c_str(), admin_code.c_str()
    };
    PGresult* r = PQexecParams(c,
        "CALL sp_app_register($1,$2,$3,$4)",
        4, nullptr, params, nullptr, nullptr, 0);
    bool ok = (PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
    PQfinish(c);
    return ok;
}

struct Fight {
    int         id;
    std::string date, fight_time, event, location;
    std::string card_type, weight_class, fighter_1, fighter_2, winner;
};

static std::vector<Fight> fetch_fights(PGresult* r) {
    std::vector<Fight> v;
    for (int i = 0; i < PQntuples(r); i++) {
        Fight f;
        f.id          = std::stoi(PQgetvalue(r,i,0));
        f.date        = PQgetvalue(r,i,1);
        f.fight_time  = PQgetvalue(r,i,2);
        f.event       = PQgetvalue(r,i,3);
        f.location    = PQgetvalue(r,i,4);
        f.card_type   = PQgetvalue(r,i,5);
        f.weight_class= PQgetvalue(r,i,6);
        f.fighter_1   = PQgetvalue(r,i,7);
        f.fighter_2   = PQgetvalue(r,i,8);
        f.winner      = PQgetvalue(r,i,9);
        v.push_back(f);
    }
    return v;
}

static void print_fights(const std::vector<Fight>& fights) {
    if (fights.empty()) {
        std::cout << YELLOW << "  (no records)\n" << RESET;
        return;
    }
    std::cout << BOLD
              << std::left
              << std::setw(5)  << "ID"
              << std::setw(12) << "Date"
              << std::setw(7)  << "Time"
              << std::setw(32) << "Event"
              << std::setw(22) << "Location"
              << std::setw(17) << "Card"
              << std::setw(20) << "Division"
              << std::setw(22) << "Fighter 1"
              << std::setw(22) << "Fighter 2"
              << std::setw(22) << "Winner"
              << "\n" << RESET;
    std::cout << GRAY << std::string(183, '-') << "\n" << RESET;
    for (auto& f : fights) {
        std::cout << std::left
                  << std::setw(5)  << f.id
                  << std::setw(12) << f.date
                  << std::setw(7)  << f.fight_time
                  << std::setw(32) << f.event.substr(0,31)
                  << std::setw(22) << f.location.substr(0,21)
                  << std::setw(17) << f.card_type.substr(0,16)
                  << std::setw(20) << f.weight_class.substr(0,19)
                  << std::setw(22) << f.fighter_1.substr(0,21)
                  << std::setw(22) << f.fighter_2.substr(0,21)
                  << std::setw(22) << f.winner.substr(0,21)
                  << "\n";
    }
    std::cout << GRAY << std::string(183, '-') << "\n" << RESET;
    std::cout << "  " << fights.size() << " record(s)\n";
}

static void show_all() {
    PGresult* r = PQexec(g_conn, "SELECT * FROM fn_get_all()");
    if (PQresultStatus(r) != PGRES_TUPLES_OK) {
        std::cout << RED << res_error(r) << "\n" << RESET;
    } else {
        print_fights(fetch_fights(r));
    }
    PQclear(r);
}

static void search_by_event() {
    std::string q = input("Search event name (partial match): ");
    const char* params[1] = { q.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "SELECT * FROM fn_search_by_event($1)",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) != PGRES_TUPLES_OK) {
        std::cout << RED << res_error(r) << "\n" << RESET;
    } else {
        print_fights(fetch_fights(r));
    }
    PQclear(r);
}

static const char* CARD_TYPES[] = {
    "Main Card", "Preliminary Card", "Early Prelims", nullptr };
static const char* WEIGHT_CLASSES[] = {
    "Flyweight","Bantamweight","Featherweight","Lightweight","Welterweight",
    "Middleweight","Light Heavyweight","Heavyweight",
    "Women's Strawweight","Women's Flyweight","Women's Bantamweight","Women's Featherweight",
    nullptr };

static std::string pick_option(const char** options, const std::string& prompt) {
    std::cout << CYAN << prompt << ":\n" << RESET;
    int n = 0;
    for (int i = 0; options[i]; i++)
        std::cout << "  " << (i+1) << ". " << options[i] << "\n", n++;
    int choice = input_int("Choice [1-" + std::to_string(n) + "]: ");
    if (choice < 1 || choice > n) { std::cout << RED << "Invalid.\n" << RESET; return ""; }
    return options[choice-1];
}

static bool fill_fight_fields(
        std::string& date, std::string& fight_time, std::string& event,
        std::string& location, std::string& card_type, std::string& weight_class,
        std::string& fighter_1, std::string& fighter_2, std::string& winner) {
    date        = input("Date (YYYY-MM-DD): ");
    fight_time  = input("Fight time (M:SS or MM:SS): ");
    event       = input("Event name: ");
    location    = input("Location: ");
    card_type   = pick_option(CARD_TYPES, "Card type");
    if (card_type.empty()) return false;
    weight_class = pick_option(WEIGHT_CLASSES, "Weight class");
    if (weight_class.empty()) return false;
    fighter_1   = input("Fighter 1: ");
    fighter_2   = input("Fighter 2: ");
    winner      = input("Winner (or blank for NC/Draw): ");
    return true;
}

static void add_fight() {
    std::string date,ft,ev,loc,ct,wc,f1,f2,win;
    if (!fill_fight_fields(date,ft,ev,loc,ct,wc,f1,f2,win)) return;
    const char* params[9] = {
        date.c_str(), ft.c_str(), ev.c_str(), loc.c_str(),
        ct.c_str(), wc.c_str(), f1.c_str(), f2.c_str(), win.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "CALL sp_add_fight($1,$2,$3,$4,$5,$6,$7,$8,$9)",
        9, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "Fight added successfully.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void update_fight() {
    int id = input_int("Enter fight ID to update: ");
    std::string sid = std::to_string(id);

    const char* p1[1] = { sid.c_str() };
    PGresult* rg = PQexecParams(g_conn, "SELECT * FROM fn_get_by_id($1)",
        1, nullptr, p1, nullptr, nullptr, 0);
    if (PQresultStatus(rg) != PGRES_TUPLES_OK || PQntuples(rg) == 0) {
        std::cout << RED << "Record not found.\n" << RESET;
        PQclear(rg); return;
    }
    print_fights(fetch_fights(rg));
    PQclear(rg);
    std::cout << "Enter new values:\n";
    std::string date,ft,ev,loc,ct,wc,f1,f2,win;
    if (!fill_fight_fields(date,ft,ev,loc,ct,wc,f1,f2,win)) return;
    const char* params[10] = {
        sid.c_str(), date.c_str(), ft.c_str(), ev.c_str(), loc.c_str(),
        ct.c_str(), wc.c_str(), f1.c_str(), f2.c_str(), win.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "CALL sp_update_fight($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)",
        10, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "Fight updated.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void delete_by_id() {
    int id = input_int("Enter fight ID to delete: ");
    std::string sid = std::to_string(id);
    const char* params[1] = { sid.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "CALL sp_delete_by_id($1)", 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "Deleted.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void delete_by_event() {
    std::string ev = input("Event name to delete: ");
    const char* params[1] = { ev.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "CALL sp_delete_by_event($1)", 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "All fights for '" << ev << "' deleted.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void clear_table() {
    std::string confirm = input("Type YES to clear all records: ");
    if (confirm != "YES") { std::cout << YELLOW << "Cancelled.\n" << RESET; return; }
    PGresult* r = PQexec(g_conn, "CALL sp_clear_table()");
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "Table cleared.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void create_user() {
    std::string uname = input("New username: ");
    std::string upass = input("Password: ");
    std::string role  = pick_option(new const char*[3]{"admin","guest",nullptr}, "Role");
    if (role.empty()) return;
    const char* params[3] = { uname.c_str(), upass.c_str(), role.c_str() };
    PGresult* r = PQexecParams(g_conn,
        "CALL sp_create_user($1,$2,$3)",
        3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_COMMAND_OK)
        std::cout << GREEN << "User '" << uname << "' created with role '" << role << "'.\n" << RESET;
    else
        std::cout << RED << res_error(r) << "\n" << RESET;
    PQclear(r);
}

static void print_main_menu() {
    std::cout << "\n" << BOLD << CYAN
              << "══════════════════════════════════════\n"
              << "  UFC Fight Database  │  " << g_db_name
              << "\n  User: " << g_app_user
              << "  │  Role: " << YELLOW << g_role << CYAN
              << "\n══════════════════════════════════════\n" << RESET;
    std::cout << "  1. Show all fights\n";
    std::cout << "  2. Search by event\n";
    std::cout << "  3. Add fight\n";
    std::cout << "  4. Update fight\n";
    std::cout << "  5. Delete fight by ID\n";
    std::cout << "  6. Delete fights by event\n";
    std::cout << "  7. Clear table\n";
    std::cout << "  8. Create new database\n";
    std::cout << "  9. Drop current database\n";
    std::cout << " 10. Create new user\n";
    std::cout << "  0. Logout\n";
    std::cout << GRAY << "══════════════════════════════════════\n" << RESET;
}

static void main_loop() {
    while (true) {
        print_main_menu();
        std::string choice = input("Choice: ");
        int c = 0;
        try { c = std::stoi(choice); } catch (...) { continue; }

        if (c == 0) {
            if (g_conn) { PQfinish(g_conn); g_conn = nullptr; }
            g_db_name.clear(); g_app_user.clear(); g_role.clear();
            std::cout << YELLOW << "Logged out.\n" << RESET;
            return;
        }
        if (c == 1) { show_all(); wait_enter(); continue; }
        if (c == 2) { search_by_event(); wait_enter(); continue; }

        switch (c) {
            case 3:  add_fight();    break;
            case 4:  update_fight(); break;
            case 5:  delete_by_id(); break;
            case 6:  delete_by_event(); break;
            case 7:  clear_table();  break;
            case 8: {
                std::string name = input("New database name: ");
                if (create_database(name))
                    std::cout << GREEN << "Database '" << name
                              << "' created. Default accounts: admin/adminpass, guest/guestpass\n" << RESET;
                break;
            }
            case 9: {
                std::cout << RED << "Drop database '" << g_db_name << "'? Type YES to confirm: " << RESET;
                std::string confirm; std::getline(std::cin, confirm);
                if (confirm == "YES") {
                    std::string dropped = g_db_name;
                    if (drop_database(dropped)) {
                        std::cout << GREEN << "Database '" << dropped << "' dropped.\n" << RESET;
                        g_db_name.clear(); g_app_user.clear(); g_role.clear();
                        return;
                    }
                } else std::cout << YELLOW << "Cancelled.\n" << RESET;
                break;
            }
            case 10: create_user(); break;
            default: std::cout << RED << "Invalid option.\n" << RESET;
        }
        wait_enter();
    }
}

static void login_screen() {
    while (true) {
        clear_screen();
        auto dbs = list_databases();

        std::cout << BOLD << CYAN
                  << "╔══════════════════════════════════╗\n"
                  << "║     UFC Fight Database — C++     ║\n"
                  << "╚══════════════════════════════════╝\n" << RESET;
        std::cout << "  1. Login\n";
        std::cout << "  2. Register\n";
        std::cout << "  3. Create new database\n";
        std::cout << "  0. Exit\n";
        std::cout << GRAY << "  Available databases: ";
        for (auto& d : dbs) std::cout << d << "  ";
        std::cout << "\n" << RESET;

        std::string choice = input("\nChoice: ");
        int c = 0;
        try { c = std::stoi(choice); } catch (...) { continue; }

        if (c == 0) { std::cout << "Bye.\n"; exit(0); }

        if (c == 1) {
            std::string db   = input("Database name: ");
            std::string user = input("Username: ");
            std::string pass = input_password("Password: ");
            if (app_login(db, user, pass)) {
                std::cout << GREEN << "Logged in as " << user
                          << " (" << g_role << ") on " << db << "\n" << RESET;
                main_loop();
            } else {
                wait_enter();
            }
        }
        else if (c == 2) {
            std::string db   = input("Database name: ");
            std::string user = input("Username (min 3 chars): ");
            std::string pass = input_confirm_password();
            const char* roles[3] = {"admin","guest",nullptr};
            std::string role = pick_option(roles, "Role");
            if (role.empty()) continue;
            std::string code;
            if (role == "admin") code = input_password("Admin secret code: ");
            if (app_register(db, user, pass, role, code))
                std::cout << GREEN << "Registered! You can now login.\n" << RESET;
            wait_enter();
        }
        else if (c == 3) {
            std::string name = input("New database name: ");
            if (create_database(name))
                std::cout << GREEN << "Done. Default accounts: admin/adminpass, guest/guestpass\n" << RESET;
            wait_enter();
        }
    }
}

int main() {
    g_os_user = get_os_user();
    std::cout << GRAY << "(OS user: " << g_os_user << ")\n" << RESET;
    login_screen();
    return 0;
}
