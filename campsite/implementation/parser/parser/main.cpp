/******************************************************************************

CAMPSITE is a Unicode-enabled multilingual web content
management system for news publications.
CAMPFIRE is a Unicode-enabled java-based near WYSIWYG text editor.
Copyright (C)2000,2001  Media Development Loan Fund
contact: contact@campware.org - http://www.campware.org
Campware encourages further development. Please let us know.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

******************************************************************************/

/******************************************************************************

Contains the main function, initialisation functions and functions performing
certain operations against database: subscription, login, change user
information, search articles.

The main function builds the context from cgi parameters and from database,
calls the initialisation functions, eventually the functions performing
operations against database, creates a parser hash, creates a parser object
for the requested template and calls Parse and WriteOutput methods of parser
object.

******************************************************************************/

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <exception>
#include <pwd.h>
#include <grp.h>

#include "lex.h"
#include "atoms.h"
#include "parser.h"
#include "util.h"
#include "threadpool.h"
#include "cms_types.h"
#include "srvdef.h"
#include "csocket.h"
#include "ccampsiteinstance.h"
#include "thread.h"
#include "process_req.h"
#include "cpublication.h"
#include "cpublicationsregister.h"
#include "curlshortnames.h"
#include "curltemplatepath.h"
#include "cmessagefactory.h"

using std::cout;
using std::cerr;
using std::endl;
using std::flush;


CMessage* readMessage(CTCPSocket* p_pcoClSock, CMessageFactoryRegister& p_rcoMFReg)
{
	char pchContent[11];

	int nMsgLen = p_pcoClSock->Recv(pchContent, 10, 0);
	if (nMsgLen < 9)
		throw SocketErrorException("Receive error");

	pchContent[10] = 0;
	uint nDataSize = strtol(pchContent + 5, NULL, 16);
	char *pchMsg = new char[nDataSize + 10];
	memcpy(pchMsg, pchContent, 10);
	p_pcoClSock->Recv(pchMsg + 10, nDataSize, 0);
	pchMsg[nDataSize + 10] = 0;

	return p_rcoMFReg.createMessage(pchMsg);
}


void resetPublicationsCache(const CMsgResetCache* p_pcoMsg)
{
	string coOperation = p_pcoMsg->getParameter("operation")->asString();
	id_type nPublicationId = Integer(p_pcoMsg->getParameter(P_IDPUBL)->asString());

	if (coOperation == "delete" || coOperation == "modify")
		CPublicationsRegister::getInstance().erase(nPublicationId);
	if (coOperation == "create" || coOperation == "modify")
		new CPublication(nPublicationId, MYSQLConnection());
}


void resetTopicsCache(const CMsgResetCache* p_pcoMsg)
{
	bool bUpdated;
	UpdateTopics(bUpdated);
	if (bUpdated)
		CParser::resetMap();
}


void resetArticleTypesCache(const CMsgResetCache* p_pcoMsg)
{
	if (CLex::updateArticleTypes())
		CParser::resetMap();
}


void resetAllCache(const CMsgResetCache* p_pcoMsg)
{
	resetPublicationsCache(p_pcoMsg);
	resetTopicsCache(p_pcoMsg);
	resetArticleTypesCache(p_pcoMsg);
}


void resetCache(const CMsgResetCache* p_pcoMsg)
{
	string coType = p_pcoMsg->getType();
	if (coType == "all")
		resetAllCache(p_pcoMsg);
	if (coType == "publications")
		resetPublicationsCache(p_pcoMsg);
	if (coType == "topics")
		resetTopicsCache(p_pcoMsg);
	if (coType == "article_types")
		resetArticleTypesCache(p_pcoMsg);
}


int readPublications()
{
	MYSQL* pSQL = MYSQLConnection();
	SQLQuery(pSQL, "select Id from Publications");
	StoreResult(pSQL, coRes);
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(*coRes)) != NULL)
		new CPublication(Integer(row[0]), pSQL);
	return RES_OK;
}


// MyThreadRoutine: thread routine; this is started on new thread start
// Parameters:
//		void* p_pArg - pointer to connection to client socket
void* MyThreadRoutine(void* p_pArg)
{
	if (p_pArg == 0)
	{
		cout << "MyThreadRoutine: Invalid arg\n";
		return NULL;
	}

	// block all signals
	sigset_t nSigMask;
	sigfillset(&nSigMask);
	pthread_sigmask(SIG_SETMASK, &nSigMask, NULL);

	CAction::initTempMembers();
	CTCPSocket* pcoClSock = (CTCPSocket*)p_pArg;
	struct timeval tVal = { 0, 0 };
	tVal.tv_sec = 5;
	fd_set clSet;
	FD_ZERO(&clSet);
	FD_SET((SOCKET)*pcoClSock, &clSet);
	MYSQL* pSql = NULL;
	try
	{
		if (select(FD_SETSIZE, &clSet, NULL, NULL, &tVal) == -1
		    || !FD_ISSET((SOCKET)*pcoClSock, &clSet))
		{
			throw RunException("Error on select");
		}
		CMessage* pcoMessage = readMessage(pcoClSock, CMessageFactoryRegister::getInstance());
		if (pcoMessage->getMessageTypeId() == 2) {
			resetCache((CMsgResetCache*)pcoMessage);
			return NULL;
		}
		if (pcoMessage->getMessageTypeId() != 1)
			return NULL;
		string coAlias = ((CMsgURLRequest*)pcoMessage)->getHTTPHost();
		const CPublication* pcoPub = CPublicationsRegister::getInstance().getPublication(coAlias);
		const CURLType* pcoURLType = pcoPub->getURLType();
		CURL* pcoURL = pcoURLType->getURL(*((CMsgURLRequest*)pcoMessage));
		string coRemoteAddress = ((CMsgURLRequest*)pcoMessage)->getRemoteAddress();

		outbuf coOutBuf((SOCKET)*pcoClSock);
		sockstream coOs(&coOutBuf);
		pSql = MYSQLConnection();
		if (pSql == NULL)		// unable to connect to server
		{
			coOs << "<html><head><title>REQUEST ERROR</title></head>\n"
			        "<body>Unable to connect to database server.</body></html>\n";
		}
		else
		{
			RunParser(MYSQLConnection(), pcoURL, coRemoteAddress.c_str(), coOs);
		}
		coOs.flush();
		delete pcoClSock;
	}
	catch (RunException& coEx)
	{
		delete pcoClSock;
#ifdef _DEBUG
		cerr << "MyThreadRoutine: " << coEx.what() << endl;
#endif
	}
	catch (SocketErrorException& coEx)
	{
		delete pcoClSock;
#ifdef _DEBUG
		cerr << "MyThreadRoutine: " << coEx.Message() << endl;
#endif
	}
	catch (out_of_range& coEx)
	{
		delete pcoClSock;
#ifdef _DEBUG
		cerr << "MyThreadRoutine: " << coEx.what() << endl;
#endif
	}
	catch (bad_alloc& coEx)
	{
		delete pcoClSock;
#ifdef _DEBUG
		cerr << "MyThreadRoutine: unable to allocate memory: " << coEx.what() << endl;
#endif
	}
	catch (exception& coEx)
	{
		delete pcoClSock;
#ifdef _DEBUG
		cerr << coEx.what() << endl;
#endif
	}
	return NULL;
}

// nMainThreadPid: pid of main thread
int nMainThreadPid;

// SigHandler: TERM signal handler
void SigHandler(int p_nSig)
{
	if (nMainThreadPid != 0)
	{
		kill(nMainThreadPid, SIGTERM);
		nMainThreadPid = 0;
	}
	exit(0);
}

// StartDaemon: run in background
void StartDaemon()
{
	if (fork() != 0)
		exit(0);
	setsid();
}

// ProcessArgs: process command line arguments
//		int argc - arguments number
//		char** argv - arguments list
//		bool& p_rbRunAsDaemon - set by this function according to arguments
//		string& p_rcoConfDir - set by this function according to arguments
void ProcessArgs(int argc, char** argv, bool& p_rbRunAsDaemon, string& p_rcoConfDir)
{
	if (argc < 2)
		return ;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-d") == 0)
			p_rbRunAsDaemon = false;
		if (strcmp(argv[i], "-c") == 0)
		{
			if (++i < argc)
			{
				cout << "You did not specify the configuration directory.";
				exit(1);
			}
			else
			{
				p_rcoConfDir = atoi(argv[i]);
			}
		}
		if (strcmp(argv[i], "-h") == 0)
		{
			cout << "Usage: campsite_server [-c <conf_dir>|-d|-h]\n"
					"where:\t-d: run in console (by default run as daemon)\n"
					"\t-c <conf_dir>: set the configuration directory\n"
					"\t-h: print this help message" << endl;
			exit(0);
		}
	}
}

#if (__GNUC__ < 3)
void my_terminate()
{
	cout << "uncought exception. terminate." << endl;
	abort();
}
#endif

CThreadPool* g_pcoThreadPool = NULL;

void sigterm_handler(int p_nSigNum)
{
	cout << p_nSigNum << " signal received (child)" << endl;
#ifdef _DEBUG
	cout << p_nSigNum << " signal received" << endl;
#endif
	if (g_pcoThreadPool == NULL)
	{
#ifdef _DEBUG
		cout << "pointer to thread pool object is null" << endl;
#endif
		exit(0);
	}
	uint nWorkingThreads = g_pcoThreadPool->workingThreads();
	if (nWorkingThreads == 0)
	{
#ifdef _DEBUG
		cout << "there are no working threads" << endl << "closing all sockets" << endl;
#endif
		CSocket::closeAllSockets();
		exit(0);
	}
#ifdef _DEBUG
	cout << "waiting for " << nWorkingThreads << " thread(s) to finish" << endl;
#endif
	for (int i = 1; i < 20 && nWorkingThreads > 0; i++)
	{
		usleep(300000);
		nWorkingThreads = g_pcoThreadPool->workingThreads();
	}
#ifdef _DEBUG
	cout << "killing idle threads" << endl;
#endif
	g_pcoThreadPool->killIdleThreads();
#ifdef _DEBUG
	cout << endl << "closing all sockets" << endl;
#endif
	CSocket::closeAllSockets();
	if (nWorkingThreads > 0)
	{
#ifdef _DEBUG
		cerr << "killing all threads" << endl;
#endif
		g_pcoThreadPool->killAllThreads();
	}
	exit(0);
}


void parent_sig_handler(int p_nSigNum)
{
	cout << p_nSigNum << " signal received (parent)" << endl;

	const CCampsiteInstanceMap& rcoInstances =
			CCampsiteInstanceRegister::get().getCampsiteInstances();
	CCampsiteInstanceMap::const_iterator coIt = rcoInstances.begin();
	for (; coIt != rcoInstances.end(); ++coIt)
	{
		cout << "stopping instance: " << (*coIt).second->getName() << endl;
		(*coIt).second->stop();
	}

	exit(0);
}


void set_signals(sighandler_t p_sigHandler, bool p_bSetTERM = true, 
				 bool p_bSetHUP = true, bool p_bSetINT = true)
{
	// mask most signals
	sigset_t nSigMask;
	sigemptyset(&nSigMask);
	sigaddset(&nSigMask, SIGPIPE);
	sigaddset(&nSigMask, SIGALRM);
	sigaddset(&nSigMask, SIGUSR1);
	sigaddset(&nSigMask, SIGUSR2);
	pthread_sigmask(SIG_SETMASK, &nSigMask, NULL);

	// set the signal handlers
	if (p_bSetTERM)
		signal(SIGTERM, p_sigHandler);
	if (p_bSetHUP)
		signal(SIGHUP, p_sigHandler);
	if (p_bSetINT)
		signal(SIGINT, p_sigHandler);
}


int CampsiteInstanceFunc(const ConfAttrValue& p_rcoConfValues);


// main: main function
// Return 0 if no error encountered; error code otherwise
// Parameters:
//		int argc - arguments number
//		char** argv - arguments list
int main(int argc, char** argv)
{
	bool bRunAsDaemon;
	string coConfDir;
	ProcessArgs(argc, argv, bRunAsDaemon, coConfDir);
	if (bRunAsDaemon)
		StartDaemon();

	set_signals(parent_sig_handler);

	if (coConfDir == "")
		coConfDir = ETC_FILE;
	const CCampsiteInstanceMap& rcoInstances =
			CCampsiteInstance::readFromDirectory(coConfDir, CampsiteInstanceFunc);

	CCampsiteInstanceMap::const_iterator coIt = rcoInstances.begin();
	for (; coIt != rcoInstances.end(); ++coIt)
	{
		cout << "instance: " << (*coIt).second->getName() << endl;
		(*coIt).second->run();
	}
	while (true)
	{
		int nStatus;
		pid_t nChildPID = waitpid(-1, &nStatus, 0);
		cout << "child " << nChildPID << " exited with status " << nStatus << endl;
		try {
			CCampsiteInstance* pcoInstance = 
					CCampsiteInstanceRegister::get().getCampsiteInstance(nChildPID);
			pcoInstance->run();
		}
		catch (InvalidValue& rcoEx) {
			cout << "child " << nChildPID << " not found" << endl;
		}
	}
	exit(0);
}

int CampsiteInstanceFunc(const ConfAttrValue& p_rcoConfValues)
{
	nMainThreadPid = 0;
	int nMaxThreads;
	int nPort;
	int nUserId;
	int nGroupId;

	nMaxThreads = atoi(p_rcoConfValues.valueOf("PARSER_MAX_THREADS").c_str());
	nPort = atoi(p_rcoConfValues.valueOf("PARSER_PORT").c_str());

	const char* pUser = p_rcoConfValues.valueOf("APACHE_USER").c_str();
	struct passwd* pPwEnt = getpwnam(pUser);
	if (pPwEnt == NULL)
	{
		cerr << "Invalid user name in conf file";
		exit(1);
	}
	nUserId = pPwEnt->pw_uid;
	const char* pGroup = p_rcoConfValues.valueOf("APACHE_GROUP").c_str();
	struct group* pGrEnt = getgrnam(pGroup);
	if (pGrEnt == NULL)
	{
		cerr << "Invalid group name in conf file";
		exit(1);
	}
	nGroupId = pGrEnt->gr_gid;

	SQL_SERVER = p_rcoConfValues.valueOf("DATABASE_SERVER_ADDRESS");
	SQL_SRV_PORT = atoi(p_rcoConfValues.valueOf("DATABASE_SERVER_PORT").c_str());
	SQL_USER = p_rcoConfValues.valueOf("DATABASE_USER");
	SQL_PASSWORD = p_rcoConfValues.valueOf("DATABASE_PASSWORD");
	SQL_DATABASE = p_rcoConfValues.valueOf("DATABASE_NAME");

	cout << "max threads: " << nMaxThreads << ", port: " << nPort << ", user id: "
			<< nUserId << ", group id: " << nGroupId << endl;
	cout << "sql server: " << SQL_SERVER << ", sql port: " << SQL_SRV_PORT
			<< ", sql user: " << SQL_USER << ", sql password: " << SQL_PASSWORD
			<< ", db name: " << SQL_DATABASE << endl;

	nPort = nPort > 0 ? nPort : 2001;
	nMaxThreads = nMaxThreads > 0 ? nMaxThreads : MAX_THREADS;
// 	if (setuid(nUserId) != 0)
// 	{
// 		cout << "Error setting user id " << nUserId << endl;
// 		exit (1);
// 	}
// 	if (setgid(nGroupId) != 0)
// 	{
// 		cout << "Error setting group id " << nGroupId << endl;
// 		exit (1);
// 	}

	set_signals(sigterm_handler, true, true, false);

#if (__GNUC__ < 3)
	set_terminate(my_terminate);
#else
	// The __verbose_terminate_handler function obtains the name of the current exception, 
	// attempts to demangle it, and prints it to stderr. If the exception is derived from
	// std::exception then the output from what() will be included. 
	std::set_terminate (__gnu_cxx::__verbose_terminate_handler);
#endif
	try
	{
		// initialize topics cache
		bool nTopicsChanged = false;
		UpdateTopics(nTopicsChanged);

		// initialize article types cache
		CLex::updateArticleTypes();

		// initialize publications cache
		readPublications();

		// initilize URL types
		new CURLShortNamesType();
		new CURLTemplatePathType();

		// initialize message types
		CMessageFactoryRegister::getInstance().insert(new CURLRequestMessageFactory());
		CMessageFactoryRegister::getInstance().insert(new CResetCacheMessageFactory());
		CMessageFactoryRegister::getInstance().insert(new CRestartServerMessageFactory());

		CServerSocket coServer("0.0.0.0", nPort);
		g_pcoThreadPool = new CThreadPool(1, nMaxThreads, MyThreadRoutine, NULL);
		CTCPSocket* pcoClSock = NULL;
		for (; ; )
		{
			try
			{
				pcoClSock = coServer.Accept();
				char* pchRemoteIP = pcoClSock->RemoteIP();
				if (pchRemoteIP != "127.0.0.1")
				{
					cerr << "Not allowed host (" << pchRemoteIP << ") connected" << endl;
					delete pcoClSock;
					continue;
				}
				if (pcoClSock == 0)
					throw SocketErrorException("Accept error");
				g_pcoThreadPool->waitFreeThread();
				g_pcoThreadPool->startThread(true, (void*)pcoClSock);
			}
			catch (ExThread& coEx)
			{
				pcoClSock->Shutdown();
				delete pcoClSock;
				cerr << "Error starting thread: " << coEx.Message() << endl;
			}
			catch (SocketErrorException& coEx)
			{
				cerr << "Socket (" << (SOCKET)*pcoClSock << ") error: " << coEx.Message() << endl;
				pcoClSock->Shutdown();
				delete pcoClSock;
			}
		}
	}
	catch (ExMutex& rcoEx)
	{
		cerr << rcoEx.Message() << endl;
		return 1;
	}
	catch (ExThread& rcoEx)
	{
		cerr << "Thread: " << rcoEx.ThreadId() << ", Severity: " << rcoEx.Severity()
		     << "; " << rcoEx.Message() << endl;
		return 2;
	}
	catch (SocketErrorException& rcoEx)
	{
		cerr << rcoEx.Message() << endl;
		return 3;
	}
	catch (exception& rcoEx)
	{
		cerr << "exception: " << rcoEx.what() << endl;
		return 4;
	}
	catch (...)
	{
		cerr << "unknown exception" << endl;
		return 5;
	}
	return 0;
}
