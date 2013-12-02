/*

MEGA SDK SQLite DB access layer

(c) 2013 by Mega Limited, Wellsford, New Zealand

Author: js

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include "mega.h"

namespace mega {

SqliteDbAccess::SqliteDbAccess(string* path)
{
    if (path) dbpath = *path;
    db = NULL;
}

SqliteDbAccess::~SqliteDbAccess()
{
	if (db) sqlite3_close(db);
}

DbTable* SqliteDbAccess::open(FileSystemAccess* fsaccess, string* name)
{
	if (db)
	{
		sqlite3_close(db);
		db = NULL;
	}

	string dbdir = dbpath + "megaclient_statecache_" + *name + ".db";

	int rc;

	rc = sqlite3_open(dbdir.c_str(),&db);

	if (rc) return NULL;

	const char *sql = "CREATE TABLE IF NOT EXISTS statecache ( id INTEGER PRIMARY KEY ASC NOT NULL, content BLOB NOT NULL)";

	rc = sqlite3_exec(db,sql,NULL,NULL,NULL);

	if (rc) return NULL;

	return new SqliteDbTable(db);
}

SqliteDbTable::SqliteDbTable(sqlite3* cdb)
{
	db = cdb;
	pStmt = NULL;
}

SqliteDbTable::~SqliteDbTable()
{
	if (pStmt) sqlite3_finalize(pStmt);
	abort();
}

// set cursor to first record
void SqliteDbTable::rewind()
{
	if (pStmt) sqlite3_reset(pStmt);
	else sqlite3_prepare(db,"SELECT id, content FROM statecache",-1,&pStmt,NULL);
}

// retrieve next record through cursor
bool SqliteDbTable::next(uint32_t* index, string* data)
{
	if (!pStmt) return false;

	int rc = sqlite3_step(pStmt);

	if (rc != SQLITE_ROW)
	{
		sqlite3_finalize(pStmt);
		pStmt = NULL;
		return false;
	}

	*index = sqlite3_column_int(pStmt,0);

	data->assign((char*)sqlite3_column_blob(pStmt,1),sqlite3_column_bytes(pStmt,1));

	return true;
}

// retrieve record by index
bool SqliteDbTable::get(uint32_t index, string* data)
{
	sqlite3_stmt *stmt;

	int rc = sqlite3_prepare(db,"SELECT content FROM statecache WHERE ID = ?",-1,&stmt,NULL);

	if (rc) return false;

	rc = sqlite3_bind_int(stmt,1,index);

	if (rc) return false;

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) return false;

	data->assign((char*)sqlite3_column_blob(stmt,0),sqlite3_column_bytes(stmt,0));

	sqlite3_finalize(stmt);

	return true;
}

// add/update record by index
bool SqliteDbTable::put(uint32_t index, char* data, unsigned len)
{
	sqlite3_stmt *stmt;

	int rc = sqlite3_prepare(db,"INSERT OR REPLACE INTO statecache ( id, content ) VALUES ( ?, ? )",-1,&stmt,NULL);
	if (rc) return false;

	rc = sqlite3_bind_int(stmt,1,index);
	if (rc) return false;

	rc = sqlite3_bind_blob(stmt,2,data,len,SQLITE_STATIC);
	if (rc) return false;

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) return false;

	sqlite3_finalize(stmt);
	return true;
}

// delete record by index
bool SqliteDbTable::del(uint32_t index)
{
	char buf[64];

	sprintf(buf,"DELETE FROM statecache WHERE id = %" PRIu32,index);

	return !sqlite3_exec(db,buf,0,0,NULL);
}

// truncate table
void SqliteDbTable::truncate()
{
	sqlite3_exec(db,"DELETE FROM statecache",0,0,NULL);
}

// begin transaction
void SqliteDbTable::begin()
{
	sqlite3_exec(db,"BEGIN",0,0,NULL);
}

// commit transaction
void SqliteDbTable::commit()
{
	sqlite3_exec(db,"COMMIT",0,0,NULL);
}

// abort transaction
void SqliteDbTable::abort()
{
	sqlite3_exec(db,"ROLLBACK",0,0,NULL);
}

} // namespace
