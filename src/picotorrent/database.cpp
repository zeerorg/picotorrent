#include "database.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <Shlwapi.h>

#include <vector>
#include <QString>

#include "../sqlite/sqlite3.h"

using pt::Database;

struct Migration
{
    std::string name;
    std::string sql;
};

static BOOL EnumMigrations(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam)
{
    std::vector<Migration>* migrations = reinterpret_cast<std::vector<Migration>*>(lParam);

    HRSRC rc = FindResource(hModule, lpszName, lpszType);
    DWORD size = SizeofResource(hModule, rc);
    HGLOBAL data = LoadResource(hModule, rc);
    const char* buffer = reinterpret_cast<const char*>(LockResource(data));

    Migration m;
    m.name = QString::fromStdWString(lpszName).toStdString();
    m.sql  = std::string(buffer);

    migrations->push_back(m);

    return TRUE;
}

Database::Statement::Statement(sqlite3_stmt* stmt)
    : m_stmt(stmt)
{
}

Database::Statement::~Statement()
{
    sqlite3_finalize(m_stmt);
}

void Database::Statement::bind(int idx, std::string const& value)
{
    sqlite3_bind_text(m_stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

void Database::Statement::execute()
{
    sqlite3_step(m_stmt);
}

int Database::Statement::getInt(int idx)
{
    return sqlite3_column_int(m_stmt, idx);
}

std::string Database::Statement::getString(int idx)
{
    return reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, idx));
}

Database::Database(std::string const& fileName)
{
    sqlite3_open(fileName.c_str(), &m_db);

    sqlite3_create_function(
        m_db,
        "get_known_folder_path",
        1,
        SQLITE_ANY,
        nullptr,
        getKnownFolderPath,
        nullptr,
        nullptr);

    sqlite3_create_function(
        m_db,
        "get_user_default_ui_language",
        0,
        SQLITE_ANY,
        nullptr,
        getUserDefaultUILanguage,
        nullptr,
        nullptr);
}

Database::~Database()
{
    sqlite3_close(m_db);
}

void Database::execute(std::string const& sql)
{
    auto stmt = statement(sql);
    stmt->execute();
}

bool Database::migrate()
{
    // create migration_history table
    const char* migrationHistory = "create table if not exists migration_history ("
            "id integer primary key,"
            "name text not null unique"
        ");";

    execute(migrationHistory);

    std::vector<Migration> migrations;

    EnumResourceNames(
        NULL,
        TEXT("DBMIGRATION"),
        &EnumMigrations,
        reinterpret_cast<LONG_PTR>(&migrations));

    execute("BEGIN TRANSACTION;");

    for (Migration const& m : migrations)
    {
        if (migrationExists(m.name))
        {
            continue;
        }

        execute(m.sql);

        auto stmt = statement("insert into migration_history (name) values (?);");
        stmt->bind(1, m.name.c_str());
        stmt->execute();
    }

    execute("COMMIT;");

    return true;
}

std::shared_ptr<Database::Statement> Database::statement(std::string const& sql)
{
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    return std::shared_ptr<Statement>(new Statement(stmt));
}

void Database::getKnownFolderPath(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    if (argc > 0)
    {
        std::string folderId((const char*)sqlite3_value_text(argv[0]));
        KNOWNFOLDERID fid = { 0 };

        if (folderId == "FOLDERID_Downloads")
        {
            fid = FOLDERID_Downloads;
        }
        else
        {
            return;
        }

        PWSTR result;
        HRESULT hr = SHGetKnownFolderPath(fid, 0, nullptr, &result);

        if (SUCCEEDED(hr))
        {
            std::string res = QString::fromStdWString(result).toStdString();
            sqlite3_result_text(ctx, res.c_str(), -1, SQLITE_TRANSIENT);
        }
    }
}

void Database::getUserDefaultUILanguage(sqlite3_context* ctx, int argc, sqlite3_value** argv)
{
    sqlite3_result_int(ctx, static_cast<int>(GetUserDefaultUILanguage()));
}

bool Database::migrationExists(std::string const& name)
{
    auto stmt = statement("select count(*) from migration_history where name = ?");
    stmt->bind(1, name.c_str());
    stmt->execute();
    return stmt->getInt(0) > 0;
}