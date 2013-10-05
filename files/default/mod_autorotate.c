/* --------------------------------------------------------------------------
 * $Id: mod_autorotate.c,v 10.3 2010/07/02 10:28:26 jascott Exp $
 * vi:set tabstop=4 sw=4:
 *
 * mod_autorotate:  Automatically rotate Apache log files
 * contact: jacob.scott@morganstanley.com
 * see: http://www.poptart.org/bin/view/Poptart/ModAutorotate
 *
 * Requires Apache 2.2
 *
 * Compiling: apxs -c mod_autorotate.c
 * You need a C99 compatible compiler!
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * -------------------------------------------------------------------------- */

#include "apr_time.h"
#include "apr_strings.h"

#define CORE_PRIVATE

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "mpm_common.h"
#include "scoreboard.h"

/* ---------  Forward declarations   ---------------------------------------*/

#define FORMAT_SZ 256

module AP_MODULE_DECLARE_DATA autorotate_module;


/* ---------  Configuration structures --------------------------------------*/

/* Valid rotate periods */
typedef enum
{
    HOURLY = 0,
    DAILY,
    WEEKLY,
    MONTHLY,
#if defined(ENABLE_PERMINUTE)
    PERMINUTE                   /* for testing */
#endif
} rotate_interval_t;


/* Map human readable periods to our enum */
typedef struct
{
    const char *szPeriod;
    rotate_interval_t ePeriod;
} period_map_t;

/* Restart methods */
typedef enum
{
    GRACEFUL = 0,
    FULL
} restartmethod_t;

typedef struct
{
    apr_pool_t *pPool;          /* Sub-pool used during compress operations */
    apr_proc_t *pProc;          /* Gzip process */
    const char *szLogPath;      /* The log currently being compressed */

    /* Queue of log files awaiting compression */
    apr_array_header_t *aCompressQueue;

    /* Compression program */
    const char *szCompressProgram;

    /* Nice level */
    int nNiceLevel;

} compress_child_info_t;


/* Directives from other modules that define log files */
typedef struct
{
    const char *szDirective;
    int nArgIndex;
} directive_map_t;


/* List of directives */
typedef struct _directive_map_list
{
    directive_map_t sDirective;
    struct _directive_map_list *pNext;
} directive_map_list_t;


/* Per-server configuration set */
typedef struct
{
    /* Do nothing if we're disabled */
    int bEnabled;

    /* Rotate period */
    rotate_interval_t eInterval;

    /* Period offset in microseconds, may be negative */
    apr_int64_t nOffset;

    /* Rotated logfile suffix format */
    char szFormat[FORMAT_SZ + 1];

    /* List of log files that we are working with */
    apr_array_header_t *aLogFiles;

    /* Time that the next rotate is due */
    apr_time_t tNextRotate;

    /* Don't rotate or compress anything when this is set */
    int bIsRotating;

    /* How to restart - full or graceful */
    restartmethod_t eRestartMethod;

    /* Number of logs to keep */
    int nKeepLogs;


    /* 
     * Compression related params
     */
    /* Path to compress program */
    char szCompressProgram[APR_PATH_MAX + 1];

    /* Compressed file suffix */
    char szCompressSuffix[APR_PATH_MAX + 1];

    /* Compress after this number of rotates */
    int nCompressAfter;

    /* Information about pending compressions */
    compress_child_info_t compressInfo;

    /* List of extra directives */
    directive_map_list_t *pDirectiveList;

} autorotate_config_t;



typedef void child_cb_func_t(int, void *, int);


/* ---------  Constants  ----------------------------------------------------*/

static const char *DEFAULT_FORMAT = "%Y%m%d-%H:%M:%S";
static const char *DEFAULT_COMPRESS_PROGRAM = "/usr/bin/gzip";
static const char *DEFAULT_COMPRESS_SUFFIX = ".gz";

static const period_map_t PERIOD_MAP[] = {
    {"Hourly", HOURLY},
    {"Daily", DAILY},
    {"Weekly", WEEKLY},
    {"Monthly", MONTHLY},
#if defined(ENABLE_PERMINUTE)
    {"PerMinute", PERMINUTE},
#endif
    {NULL}
};

/* Directives that we know define log files */
static const directive_map_t DIRECTIVE_MAP[] = {
    /* Core */
    {"ErrorLog", 1},

    /* mod_log_config */
    {"CustomLog", 1},
    {"CookieLog", 1},
    {"TransferLog", 1},

    /* mod_log_forensic */
    {"ForensicLog", 1},

    /* mod_rewrite */
    {"RewriteLog", 1},

    /* mod_cgi */
    {"ScriptLog", 1},

    {NULL}
};

/* ---------  Prototypes  ---------------------------------------------------*/

/* Directive handlers and helpers */
static const char *cmd_rotate_enabled(cmd_parms * pCmd, void *pDummy,
                                      int nArg);
static const char *cmd_add_log_directive(cmd_parms * pCmd, void *pDummy,
                                         const char *szArg1,
                                         const char *szArg2);
static const char *cmd_add_log_file(cmd_parms * pCmd, void *pDummy,
                                    const char *szArg1);
static const char *cmd_rotate_period(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg);
static const char *cmd_rotate_offset(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg);
static const char *cmd_rotate_format(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg);
static const char *cmd_rotate_compress(cmd_parms * pCmd, void *pDummy,
                                       const char *szArg);
static const char *cmd_rotate_compresssuffix(cmd_parms * pCmd, void *pDummy,
                                             const char *szArg);
static const char *cmd_rotate_nicelevel(cmd_parms * pCmd, void *pDummy,
                                        const char *szArg);
static const char *cmd_rotate_restartmethod(cmd_parms * pCmd, void *pDummy,
                                            const char *szArg);
static const char *cmd_rotate_keep(cmd_parms * pCmd, void *pDummy,
                                   const char *szArg);
static const char *cmd_rotate_compressafter(cmd_parms * pCmd, void *pDummy,
                                            const char *szArg);
/* Hook handlers */
static int monitor_func(apr_pool_t * p);
static int open_logs_func(apr_pool_t * pconf, apr_pool_t * plog,
                          apr_pool_t * ptemp, server_rec * s);

/* Module initializers */
static void register_hooks(apr_pool_t * p);
static void *create_autorotate_config(apr_pool_t * pPool,
                                      server_rec * pServer);

/* Utilities */
static void append_log_directive_list(directive_map_list_t ** ppItem,
                                      apr_pool_t * pool,
                                      const char *szDirective, int nPosition);
static apr_time_t period_start(int nCount, rotate_interval_t ePeriod);
static apr_time_t offset_period_start(int nCount, rotate_interval_t ePeriod,
                                      apr_int64_t nOffset);
static char *get_word(apr_pool_t * pool, int nWord, const char *szArgs);
static void find_log_names(autorotate_config_t * pConfig, apr_pool_t * pool,
                           server_rec * s, apr_table_t * tLogFiles,
                           ap_directive_t * node);
static int check_logfile_dates(apr_pool_t * ptemp, server_rec * s,
                               apr_array_header_t * aFiles);
static rotate_interval_t valid_period(const char *szPeriod);
static int is_fully_restarted(apr_pool_t * p);
static int do_rotate(apr_pool_t * p, apr_array_header_t * aList);
static int do_prune(apr_pool_t * p);
static void record_next_rotate_time(apr_pool_t * ptemp,
                                    autorotate_config_t * pConfig);
static void restart_server(apr_pool_t * ptemp, autorotate_config_t * pConfig);
static apr_status_t run_next_compress_child(compress_child_info_t * pData);
static child_cb_func_t compress_cb_func;
static int create_compress_queue(apr_pool_t * pconf, apr_pool_t * ptemp,
                                 autorotate_config_t * pConfig);

/* ---------  Configuration directive handlers  -----------------------------*/
/* Nasty.  The monitor hook doesn't have access to a server_rec so keep this
 * global populated
 */
static autorotate_config_t *pgConfigData = NULL;

/* ---------  Configuration directive handlers  -----------------------------*/

/*
 * Returns the rotate period given a string representing that period
 * Returns -1 if the given string is invalid
 */
static rotate_interval_t valid_period(const char *szPeriod)
{
    const period_map_t *pMap = PERIOD_MAP;

    AP_DEBUG_ASSERT(szPeriod != NULL);

    /* Iterate the period map looking for a match */
    while (pMap->szPeriod) {
        if (apr_strnatcasecmp(pMap->szPeriod, szPeriod) == 0) {
            return pMap->ePeriod;
        }

        pMap++;
    }

    /* No match */
    return -1;
}

/*
 * Create the per-server config record
 */
static void *create_autorotate_config(apr_pool_t * pPool,
                                      server_rec * pServer)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pPool != NULL);

    pConfig = apr_pcalloc(pPool, sizeof(autorotate_config_t));

    ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, pServer,
                 "create_rotate_config called for %s server",
                 pServer->is_virtual ? "vhost" : "main");

    pConfig->bEnabled = 0;
    pConfig->eInterval = MONTHLY;
    pConfig->nOffset = 0;
    apr_cpystrn(pConfig->szFormat, DEFAULT_FORMAT, FORMAT_SZ);
    apr_cpystrn(pConfig->szCompressProgram, DEFAULT_COMPRESS_PROGRAM,
                APR_PATH_MAX);
    apr_cpystrn(pConfig->szCompressSuffix, DEFAULT_COMPRESS_SUFFIX,
                APR_PATH_MAX);

    /* Initialize array of logfile names */
    pConfig->aLogFiles = apr_array_make(pPool, 5, sizeof(char *));

    pConfig->tNextRotate = 0;
    pConfig->bIsRotating = 0;
    pConfig->eRestartMethod = GRACEFUL;
    pConfig->nKeepLogs = 0;
    pConfig->nCompressAfter = 1;

    pConfig->compressInfo.pPool = NULL;
    pConfig->compressInfo.pProc = NULL;
    pConfig->compressInfo.szLogPath = NULL;
    pConfig->compressInfo.aCompressQueue = NULL;
    pConfig->compressInfo.nNiceLevel = 5;
    pConfig->pDirectiveList = NULL;

    /* Initialize the directive list from the static mapping */
    const directive_map_t *pEntry;
    for (pEntry = DIRECTIVE_MAP; pEntry->szDirective; pEntry++) {
        append_log_directive_list(&pConfig->pDirectiveList,
                                  pPool,
                                  pEntry->szDirective, pEntry->nArgIndex);
    }

    return pConfig;
}

/*
 * Process the 'AutorotateEnabled' directive
 */
static const char *cmd_rotate_enabled(cmd_parms * pCmd, void *pDummy,
                                      int nArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateEnabled only supported in the main server";
    }

    pConfig->bEnabled = nArg;
    return NULL;
}

static const char *cmd_add_log_file(cmd_parms * pCmd, void *pDummy,
                                    const char *szArg1)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg1 != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateAddLogFile only supported in the main server";
    }

    *(const char **) apr_array_push(pConfig->aLogFiles) =
        apr_pstrdup(pCmd->pool, szArg1);

    return NULL;
}


/*
 * Process the 'AutorotateAddLogDirective' directive
 */
static const char *cmd_add_log_directive(cmd_parms * pCmd, void *pDummy,
                                         const char *szArg1,
                                         const char *szArg2)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg1 != NULL);
    AP_DEBUG_ASSERT(szArg2 != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateAddLogDirective only supported in the main server";
    }

    int nPosition = 1;
    if (szArg2) {
        char *szEnd;
        nPosition = strtol(szArg2, &szEnd, 10);
        if (*szArg2 != '\0' && *szEnd == '\0' && nPosition > 0) {
            /* OK */
        }
        else {
            return "Invalid position for directive";
        }
    }

    append_log_directive_list(&pConfig->pDirectiveList, pCmd->pool, szArg1,
                              nPosition);

    return NULL;
}


/*
 * Append a log directive name/param-position structure to the end of the
 * given directive map list
 */
void
append_log_directive_list(directive_map_list_t ** ppItem, apr_pool_t * pool,
                          const char *szDirective, int nPosition)
{
    /* Find the end of the linked list, whilst trying not to get too confused
     * about the pointer-to-a-pointer stuff!
     */
    while (*ppItem) {
        /*      for debugging append code
           ap_log_perror(APLOG_MARK, APLOG_ERR, OK, pool,
           "mod_autorotate: Appending [%s], current item: [%s]",
           szDirective, (*ppItem)->sDirective.szDirective );
         */

        ppItem = &(*ppItem)->pNext;
    }

    /* Allocate and populate a new entry */
    *ppItem = apr_pcalloc(pool, sizeof(directive_map_list_t));
    (*ppItem)->pNext = NULL;
    (*ppItem)->sDirective.szDirective = apr_pstrdup(pool, szDirective);
    (*ppItem)->sDirective.nArgIndex = nPosition;
}


/*
 * Process the 'AutorotatePeriod' directive
 */
static const char *cmd_rotate_period(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg)
{
    autorotate_config_t *pConfig;
    rotate_interval_t ePeriod;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotatePeriod only supported in the main server";
    }

    /* Turn the string into a period identifier */
    ePeriod = valid_period(szArg);
    if (ePeriod == -1) {
        return apr_psprintf(pCmd->temp_pool,
                            "Invalid rotate period [%s].", szArg);
    }
    else {
        pConfig->eInterval = ePeriod;
    }

    return NULL;
}

/*
 * Process the 'AutorotateOffset' directive
 */
static const char *cmd_rotate_offset(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg)
{
    autorotate_config_t *pConfig;
    apr_int64_t nOffset;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateOffset only supported in the main server";
    }

    nOffset = apr_atoi64(szArg);
    if (errno == ERANGE) {
        return "AutorotateOffset out of range";
    }

    pConfig->nOffset = apr_time_from_sec(nOffset);

    return NULL;
}

/*
 * Process the 'AutorotateFormat' directive
 */
static const char *cmd_rotate_format(cmd_parms * pCmd, void *pDummy,
                                     const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateFormat only supported in the main server";
    }

    apr_cpystrn(pConfig->szFormat, szArg, FORMAT_SZ);

    return NULL;
}

/*
 * Process the 'AutorotateCompressProgram' directive
 */
static const char *cmd_rotate_compress(cmd_parms * pCmd, void *pDummy,
                                       const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateCompressProgram only supported in the main server";
    }

    apr_cpystrn(pConfig->szCompressProgram, szArg, APR_PATH_MAX);

    return NULL;
}

/*
 * Process the 'AutorotateCompressSuffix' directive
 */
static const char *cmd_rotate_compresssuffix(cmd_parms * pCmd, void *pDummy,
                                             const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateCompressProgram only supported in the main server";
    }

    apr_cpystrn(pConfig->szCompressSuffix, szArg, APR_PATH_MAX);

    return NULL;
}

/*
 * Process the 'AutorotateCompressNiceLevel' directive
 */
static const char *cmd_rotate_nicelevel(cmd_parms * pCmd, void *pDummy,
                                        const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return
            "AutorotateCompressNiceLevel only supported in the main server";
    }

    char *szEnd;
    int nNice = strtol(szArg, &szEnd, 10);
    if (*szArg != '\0' && *szEnd == '\0' && nNice >= 0) {
        pConfig->compressInfo.nNiceLevel = nNice;
    }
    else {
        return "Invalid compress nice level";
    }

    return NULL;
}

/*
 * Process the 'AutorotateRestartMethod' directive
 */
static const char *cmd_rotate_restartmethod(cmd_parms * pCmd, void *pDummy,
                                            const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateRestartMethod only supported in the main server";
    }

    if (apr_strnatcasecmp(szArg, "Full") == 0) {
        pConfig->eRestartMethod = FULL;
        return NULL;
    }
    if (apr_strnatcasecmp(szArg, "Graceful") == 0) {
        pConfig->eRestartMethod = GRACEFUL;
        return NULL;
    }

    return "AutorotateRestartMethod must be \"Full\" or \"Graceful\"";
}

/*
 * Process the 'AutorotateKeep' directive
 */
static const char *cmd_rotate_keep(cmd_parms * pCmd, void *pDummy,
                                   const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateKeep only supported in the main server";
    }

    apr_int64_t nKeep = apr_atoi64(szArg);
    if (errno == ERANGE || nKeep < 0) {
        return "AutorotateKeep out of range";
    }

    pConfig->nKeepLogs = nKeep;

    return NULL;
}

/*
 * Process the 'AutorotateCompressAfter' directive
 */
static const char *cmd_rotate_compressafter(cmd_parms * pCmd, void *pDummy,
                                            const char *szArg)
{
    autorotate_config_t *pConfig;

    AP_DEBUG_ASSERT(pCmd != NULL);
    AP_DEBUG_ASSERT(szArg != NULL);

    pConfig = ap_get_module_config(pCmd->server->module_config,
                                   &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    if (pCmd->server->is_virtual) {
        return "AutorotateCompressAfter only supported in the main server";
    }

    apr_int64_t nAfter = apr_atoi64(szArg);
    if (errno == ERANGE || nAfter < 0) {
        return "AutorotateRotateAfter out of range";
    }

    pConfig->nCompressAfter = nAfter;

    return NULL;
}


/*
 * Handler for the 'monitor' hook
 *
 * Runs every 10 seconds in the caretaker process.
 * Make sure that this routine is kept light weight!
 */
static int monitor_func(apr_pool_t * p)
{
    AP_DEBUG_ASSERT(pgConfigData != NULL);
    AP_DEBUG_ASSERT(p != NULL);

    /* Why wouldn't we have a valid global config ? */
    if (pgConfigData == NULL) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, OK, p,
                      "mod_autorotate: NULL configuration found - bug?");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Do nothing if we're not enabled */
    if (!pgConfigData->bEnabled)
        return DECLINED;

    /* Do nothing if we're currently rotating */
    if (pgConfigData->bIsRotating)
        return DECLINED;

    /* Don't do anything until the server is fully restarted */
    if (!is_fully_restarted(p)) {
        return DECLINED;
    }

    /* Check if a rotate is due */
    apr_time_t tNow = apr_time_now();
    if (tNow >= pgConfigData->tNextRotate) {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                      "mod_autorotate: Log rotation now due");

        /* Prune old logs */
        do_prune(p);

        /* Rotate and record whether we need a restart */
        int nNeedRestart = do_rotate(p, NULL);

        /* Signal a restart if we rotated any files */
        if (nNeedRestart) {
            restart_server(p, pgConfigData);
        }

        record_next_rotate_time(p, pgConfigData);
    }

    /* Start compressing if something has filled the queue
     * and it hasn't started yet */
    if ((pgConfigData->compressInfo.aCompressQueue != NULL) &&
        (pgConfigData->compressInfo.szLogPath == NULL)) {

        pgConfigData->compressInfo.szCompressProgram =
            pgConfigData->szCompressProgram;
        run_next_compress_child(&pgConfigData->compressInfo);

    }

    return OK;
}

static void restart_server(apr_pool_t * ptemp, autorotate_config_t * pConfig)
{
    AP_DEBUG_ASSERT(pConfig != NULL);
    AP_DEBUG_ASSERT(ptemp != NULL);

    if (pConfig->eRestartMethod == GRACEFUL) {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, ptemp,
                      "mod_autorotate: Requesting graceful restart");
        kill(getpid(), AP_SIG_GRACEFUL);
    }
    else {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, ptemp,
                      "mod_autorotate: Requesting FULL restart");
        kill(getpid(), SIGHUP);
    }
}


/*
 *  Remove old log files
 */
static int do_prune(apr_pool_t * p)
{
    AP_DEBUG_ASSERT(pgConfigData != NULL);
    AP_DEBUG_ASSERT(p != NULL);

    /* Decline to prune if KeepLogs is zero */
    if (pgConfigData->nKeepLogs == 0) {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                      "mod_autorotate: Log pruning disabled");
        return OK;
    }

    pgConfigData->bIsRotating = 1;
    ap_log_perror(APLOG_MARK, APLOG_DEBUG, OK, p,
                  "mod_autorotate: Pruning logs");
    char *szSuffix = apr_pcalloc(p, 256);


    /* Cycle through the log files */

    int i;
    char **pszLogFiles = (char **) pgConfigData->aLogFiles->elts;
    for (i = 0; i < pgConfigData->aLogFiles->nelts; i++) {
        /* File might be relative to server root */
        const char *szOrigName = ap_server_root_relative(p, pszLogFiles[i]);
        const char *szNewName = NULL;

        /* go back one period at a time, decrementing the number to keep
         * each time a log exists for that previous period.  After we get to
         * zero, start deleting for subsequent periods */

        int nCurrentPeriod = -1;
        int nNumFound = 0;
        for (nCurrentPeriod = -1; nCurrentPeriod > -100; nCurrentPeriod--) {
            apr_time_t tStart = offset_period_start(nCurrentPeriod,
                                                    pgConfigData->eInterval,
                                                    pgConfigData->nOffset);

            apr_time_exp_t tExp;
            apr_time_exp_lt(&tExp, tStart);

            apr_size_t nSuffixLen;
            apr_strftime(szSuffix, &nSuffixLen, 255,
                         pgConfigData->szFormat, &tExp);


            szNewName = apr_psprintf(p, "%s.%s", szOrigName, szSuffix);

            /* Check if a log exists for this previous period */
            apr_finfo_t fs;
            apr_status_t nStatus =
                apr_stat(&fs, szNewName, APR_FINFO_MTIME, p);
            if (nStatus == OK) {        /* File exists */
                nNumFound++;

                if (nNumFound >= pgConfigData->nKeepLogs) {
                    nStatus = apr_file_remove(szNewName, p);
                    if (nStatus == APR_SUCCESS) {
                        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                                      "mod_autorotate: Removed %s",
                                      szNewName);
                    }
                    else {
                        ap_log_perror(APLOG_MARK, APLOG_ERR, nStatus, p,
                                      "mod_autorotate: removing %s ",
                                      szNewName);
                    }
                }
            }

            szNewName = apr_psprintf(p, "%s.%s%s", szOrigName, szSuffix,
                                     pgConfigData->szCompressSuffix);

            nStatus = apr_stat(&fs, szNewName, APR_FINFO_MTIME, p);
            if (nStatus == OK) {        /* File exists */
                nNumFound++;

                if (nNumFound >= pgConfigData->nKeepLogs) {
                    nStatus = apr_file_remove(szNewName, p);
                    if (nStatus == APR_SUCCESS) {
                        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                                      "mod_autorotate: Removed %s",
                                      szNewName);
                    }
                    else {
                        ap_log_perror(APLOG_MARK, APLOG_ERR, nStatus, p,
                                      "mod_autorotate: removing %s ",
                                      szNewName);
                    }
                }
            }

        }                       /* End for (period) */

    }                           /* End for (log file) */


    pgConfigData->bIsRotating = 0;
}



/*
 * Rotate the log files, and return whether we did anything or not
 */
static int do_rotate(apr_pool_t * p, apr_array_header_t * aList)
{
    AP_DEBUG_ASSERT(pgConfigData != NULL);
    AP_DEBUG_ASSERT(p != NULL);

    int nNumRotated = 0;
    apr_status_t nStatus;

    ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                  "mod_autorotate: Rotating logs");
    pgConfigData->bIsRotating = 1;

    /* Start of the current period -+ offset */
    apr_time_t tStart = offset_period_start(-1, pgConfigData->eInterval,
                                            pgConfigData->nOffset);

    apr_time_exp_t tExp;
    apr_time_exp_lt(&tExp, tStart);

    /* Suffix to append to log file names */
    char *szSuffix = apr_pcalloc(p, 256);
    apr_size_t nSuffixLen;
    apr_strftime(szSuffix, &nSuffixLen, 255, pgConfigData->szFormat, &tExp);

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, OK, p,
                  "log file suffix: %s", szSuffix);

    /* Array of log files to use might be specified */
    if (!aList) {
        aList = pgConfigData->aLogFiles;
    }

    /* Cycle through the log files */
    char **pszLogFiles = (char **) aList->elts;

    int i;
    for (i = 0; i < aList->nelts; i++) {

        /* File might be relative to server root */
        const char *szOrigName = ap_server_root_relative(p, pszLogFiles[i]);
        const char *szNewName =
            apr_psprintf(p, "%s.%s", szOrigName, szSuffix);

        /* Do nothing if the source doesn't exist */
        apr_finfo_t fs;
        nStatus = apr_stat(&fs, szOrigName, APR_FINFO_MTIME, p);
        if (APR_STATUS_IS_ENOENT(nStatus)) {    /* File doesn't exist */
            ap_log_perror(APLOG_MARK, APLOG_DEBUG, OK, p,
                          "no file %s to rotate", szOrigName);
            continue;
        }

        /* Do nothing if the destination already exists */
        nStatus = apr_stat(&fs, szNewName, APR_FINFO_MTIME, p);
        if (nStatus == OK) {    /* File exists */
            ap_log_perror(APLOG_MARK, APLOG_WARNING, OK, p,
                          "mod_autorotate: destination already exists: %s ",
                          szNewName);
            continue;
        };

        nStatus = apr_file_rename(szOrigName, szNewName, p);
        if (nStatus == APR_SUCCESS) {
            ap_log_perror(APLOG_MARK, APLOG_INFO, OK, p,
                          "mod_autorotate: Renamed %s to %s ", szOrigName,
                          szNewName);
            nNumRotated++;
        }
        else {
            ap_log_perror(APLOG_MARK, APLOG_ERR, nStatus, p,
                          "mod_autorotate: renaming %s to %s ", szOrigName,
                          szNewName);
        }

    }


    pgConfigData->bIsRotating = 0;
    return (nNumRotated ? 1 : 0);
}


/*
 * Checks whether all children are from the server's current generation
 * If they're not then we've just been gracefully restarted and there are
 * still some children left finishing old requests
 */
static int is_fully_restarted(apr_pool_t * p)
{
    int nNumCurrent = 0, nNumOld = 0;
    static int nIsRestarted = 0;

    AP_DEBUG_ASSERT(p != NULL);

    /* Cache the result once we know we're fully up and running, to save
     * CPU cycles.  A server restart will reload this module and reset the
     * static back to 0
     */
    if (nIsRestarted) {
        return 1;
    }

    /* get the scoreboard */
    global_score *pGlobalScore = ap_get_scoreboard_global();
    if (!pGlobalScore) {
        ap_log_error(APLOG_MARK, APLOG_ERR, OK, NULL,
                     "mod_autorotate: Error finding global scoreboard record");
        return 0;
    }

    /* Check each process's generation and worker status */
    process_score *pProcScore;
    int nProc;
    for (nProc = 0; nProc < pGlobalScore->server_limit; nProc++) {
        pProcScore = ap_get_scoreboard_process(nProc);

        int nWorker;
        for (nWorker = 0; nWorker < pGlobalScore->thread_limit; nWorker++) {
            worker_score *pWorkerScore;
            pWorkerScore = ap_get_scoreboard_worker(nProc, nWorker);

            int nRes = pWorkerScore->status;

            /* For debugging the scoreboard code */
            /*
               ap_log_perror(APLOG_MARK,APLOG_DEBUG,OK,p,
               "Slot %02d  Thread %d: Gen %d, PID %d  %s %d",
               nProc, nWorker,
               pProcScore->generation,
               pProcScore->pid,
               (pProcScore->quiescing ? "Quiescing" : "Not Quiescing"),
               nRes );
             */

            if (nRes != SERVER_DEAD &&
                nRes != SERVER_STARTING && nRes != SERVER_IDLE_KILL) {

                if (pProcScore->generation ==
                    pGlobalScore->running_generation)
                    nNumCurrent++;
                else
                    nNumOld++;

            }
        }
    }

    /* We're fully restarted if there are no previous generation
     * processes around */
    if (nNumOld == 0) {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                      "mod_autorotate: Now fully restarted (generation %d)",
                      pGlobalScore->running_generation);
        nIsRestarted = 1;
        return 1;
    }

    /* If we still have old process then cary of checking */
    ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, p,
                  "mod_autorotate: Waiting for full restart: %d x current, %d x old",
                  nNumCurrent, nNumOld);
    return 0;
}


char *get_word(apr_pool_t * pool, int nWord, const char *szArgs)
{
    AP_DEBUG_ASSERT(pool != NULL);
    AP_DEBUG_ASSERT(szArgs != NULL);

    /* ap_getword_conf modifies the args to copy them first */
    char *szCopy = apr_pstrdup(pool, szArgs);
    char *szWord;
    int nArg = 1;

    /* Grab the nth argument */
    while (*szCopy && nArg <= nWord) {
        szWord = ap_getword_conf(pool, &szArgs);
        nArg++;
    }

    return (*szWord ? szWord : NULL);
}


/* Find the log file argument for all supported configuration directives
 */
static void
find_log_names(autorotate_config_t * pConfig, apr_pool_t * pool,
               server_rec * s, apr_table_t * tLogFiles, ap_directive_t * node)
{
    ap_directive_t *dir;
    char *szWord = NULL;

    /* Cycle the directives at this level */
    for (dir = node; dir; dir = dir->next) {

        /* Look for a match with the directives we handle */
        directive_map_list_t *pItem = pConfig->pDirectiveList;
        while (pItem) {

            if (apr_strnatcasecmp
                (pItem->sDirective.szDirective, dir->directive) == 0) {

                /* Get nth word of the directive arguments */
                szWord =
                    get_word(pool, pItem->sDirective.nArgIndex, dir->args);
                if (szWord) {
                    if (*szWord == '|') {
                        ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, s,
                                     "Ignoring piped log %s", szWord);
                    }
                    else {
                        apr_table_setn(tLogFiles, szWord, "");
                        ap_log_error(APLOG_MARK, APLOG_INFO, OK, s,
                                     "mod_autorotate: Log file for directive [%s] found: [%s]",
                                     dir->directive, szWord);
                    }
                }
                else {
                    ap_log_error(APLOG_MARK, APLOG_WARNING, OK, s,
                                 "mod_autorotate: No filename found for directive %s",
                                 dir->directive);
                }
            }
            else {
                /*  for debugging
                   ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, s,
                   "mod_autorotate: Config directive [%s] does not match list item [%s]",
                   dir->directive, pItem->sDirective.szDirective );
                 */
            }

            pItem = pItem->pNext;
        }

        /* Recurse into config tree */
        if (dir->first_child != NULL) {
            find_log_names(pConfig, pool, s, tLogFiles, dir->first_child);
        }
    }
}


/*
 * Check the mtime of the log files to determine if any should have been
 * rotated but weren't for some reason (such as the server being down, or
 * the rotate paramers having been changed)
 */
static int
check_logfile_dates(apr_pool_t * ptemp, server_rec * s,
                    apr_array_header_t * aFiles)
{
    char szAscTimePeriod[APR_CTIME_LEN + 1];
    char szAscTimeFile[APR_CTIME_LEN + 1];
    apr_time_t tStart;
    autorotate_config_t *pConfig;
    int n;

    /* The module configuration */
    pConfig = ap_get_module_config(s->module_config, &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    /*
     * Start of the current period -+ offset
     */
    tStart = offset_period_start(0, pConfig->eInterval, pConfig->nOffset);
    apr_ctime(szAscTimePeriod, tStart);

    ap_log_perror(APLOG_MARK, APLOG_INFO, OK, ptemp,
                  "mod_autorotate: Start of current period %s",
                  szAscTimePeriod);

    /*
     * Check each log file mtime
     */
    char **pszLogFiles = (char **) pConfig->aLogFiles->elts;

    int i;
    int bNeedRotate = 0;
    for (i = 0; i < pConfig->aLogFiles->nelts; i++) {
        /* File might be relative to server root */
        const char *pFilename =
            ap_server_root_relative(ptemp, pszLogFiles[i]);

        apr_finfo_t fs;
        apr_status_t status;
        if ((status = apr_stat(&fs, pFilename, APR_FINFO_MTIME, ptemp))
            == OK) {
            if (fs.mtime < tStart) {
                apr_ctime(szAscTimeFile, fs.mtime);
                ap_log_error(APLOG_MARK, APLOG_NOTICE, status, s,
                             "mod_autorotate: Log file %s last written before start "
                             "of this period (%s < %s).  Requesting immediate rotate.",
                             pFilename, szAscTimeFile, szAscTimePeriod);

                *(const char **) apr_array_push(aFiles) = pFilename;

                bNeedRotate = 1;
            }
        }
        else {
            if (!APR_STATUS_IS_ENOENT(status)) {
                ap_log_error(APLOG_MARK, APLOG_ERR, status, s,
                             "mod_autorotate: could not get modifiied time of %s",
                             pFilename);
            }
        }
    }

    /* The caller can choose to restart now if we've rotated */
    return bNeedRotate;
}


/*
 * Record the time we will next try a rotate - ie. the start of the
 * next period.  The monitor hook will pick this up
 * later and initiate the rotate
 */
void
record_next_rotate_time(apr_pool_t * ptemp, autorotate_config_t * pConfig)
{
    char szAscTime[APR_CTIME_LEN + 1];

    pConfig->tNextRotate = offset_period_start(1, pConfig->eInterval,
                                               pConfig->nOffset);

    apr_ctime(szAscTime, pConfig->tNextRotate);
    ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, ptemp,
                  "mod_autorotate: Next rotation due at %s", szAscTime);
}


/*
 * Handler for the 'open_logs' hook
 *
 * Runs when Apache opens its log files, in the caretaker process
 * We run this last in the chain, so that all the other modules have had
 * a chance to open their logs before we run.
 *
 * pconf: Configuration pool, cleared on restart
 * plog:  Logging streams pool, cleared after config file read
 * ptemp: Temp pool, exists during config phase only
 */
static int
open_logs_func(apr_pool_t * pconf, apr_pool_t * plog,
               apr_pool_t * ptemp, server_rec * s)
{
    int i;
    autorotate_config_t *pConfig;

    /* The module configuration */
    pConfig = ap_get_module_config(s->module_config, &autorotate_module);
    AP_DEBUG_ASSERT(pConfig != NULL);

    /* Horrendous - but the monitor hook doesn't have any other way to get
     * to the module config */
    pgConfigData = pConfig;

    /* Do nothing if we're not enabled */
    if (!pConfig->bEnabled)
        return DECLINED;

    /* Allocate a temporary table for the log file names */
    apr_table_t *tLogFiles = apr_table_make(ptemp, 5);

    /* Pull out the list of configured log files */
    find_log_names(pConfig, ptemp, s, tLogFiles, ap_conftree);

    /*
     * Table will be de-duped, now copy values to the server config
     */

    /* A table is just an array of table entries */
    const apr_array_header_t *arr = apr_table_elts(tLogFiles);
    AP_DEBUG_ASSERT(arr != NULL);
    if (arr) {
        const apr_table_entry_t *elts = (const apr_table_entry_t *) arr->elts;
        for (i = 0; i < arr->nelts; i++) {
            *(const char **) apr_array_push(pConfig->aLogFiles) =
                apr_pstrdup(pconf, elts[i].key);
            ap_log_error(APLOG_MARK, APLOG_DEBUG, OK, s,
                         "Added log file %s", elts[i].key);
        }
    }

    ap_log_error(APLOG_MARK, APLOG_NOTICE, OK, s,
                 "mod_autorotate: Operating on %d log files",
                 pConfig->aLogFiles->nelts);

    /* Prune old logs */
    do_prune(ptemp);

    /* Kick off an initial check of the log files, and rotate right now
     * if we need to.  The server restarts after the first initialisation
     * on its own as standard, so we don't need to do anything special
     * to request a restart.
     */
    apr_array_header_t *aFiles = apr_array_make(ptemp, 5, sizeof(char *));
    if (check_logfile_dates(ptemp, s, aFiles)) {
        do_rotate(ptemp, aFiles);
    }

    /* Record the date of the next required rotate */
    record_next_rotate_time(pconf, pConfig);

    /* Create a list of files that need to be compressed.
     * Note that we don't kick off the compression here, because any
     * process we create will get killed when startup phase 1 goes away.
     * We leave it to the monitor function to kick off the compresions
     * the first time it runs (which only happens when the server is
     * really up and working fully)
     */
    create_compress_queue(pconf, ptemp, pConfig);

    return OK;
}


static int
create_compress_queue(apr_pool_t * pconf, apr_pool_t * ptemp,
                      autorotate_config_t * pConfig)
{
    compress_child_info_t *pInfo = &pConfig->compressInfo;

    /* Decline to create a queue if compression is disabled */
    if (pConfig->nCompressAfter == 0) {
        ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, ptemp,
                      "mod_autorotate: Log compression disabled");
        return OK;
    }

    /* Create a sub-pool for the compress process which will be cleared when
     * done with the compressions... */
    int rc;
    if ((rc = apr_pool_create(&pInfo->pPool, pconf)) != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, rc, ptemp,
                      "mod_autorotate: Error creating sub-pool");
        pInfo->aCompressQueue = NULL;
        return rc;
    }

    pInfo->aCompressQueue = apr_array_make(pInfo->pPool, 5, sizeof(char *));

    char *szSuffix = apr_pcalloc(ptemp, 256);

    /* Cycle through the log files */
    int i;
    char **pszLogFiles = (char **) pConfig->aLogFiles->elts;
    for (i = 0; i < pConfig->aLogFiles->nelts; i++) {
        /* File might be relative to server root */
        const char *szOrigName =
            ap_server_root_relative(ptemp, pszLogFiles[i]);
        const char *szNewName = NULL;

        /* go back one period at a time, decrementing the number to keep
         * each time a log exists for that previous period.  After we get to
         * zero, start deleting for subsequent periods */

        int nCurrentPeriod = 0;
        int nNumFound = 0;
        for (nCurrentPeriod = 0; nCurrentPeriod > -100; nCurrentPeriod--) {
            apr_time_t tStart = offset_period_start(nCurrentPeriod,
                                                    pConfig->eInterval,
                                                    pConfig->nOffset);

            apr_time_exp_t tExp;
            apr_time_exp_lt(&tExp, tStart);

            apr_size_t nSuffixLen;
            apr_strftime(szSuffix, &nSuffixLen, 255,
                         pgConfigData->szFormat, &tExp);

            szNewName = apr_psprintf(ptemp, "%s.%s", szOrigName, szSuffix);

            /* Check if a log exists for this previous period */
            apr_finfo_t fs;
            apr_status_t nStatus = apr_stat(&fs, szNewName, APR_FINFO_MTIME,
                                            ptemp);
            if (nStatus == OK) {        /* File exists */
                nNumFound++;

                if (nNumFound >= pConfig->nCompressAfter) {
                    *(const char **) apr_array_push(pInfo->aCompressQueue) =
                        apr_pstrdup(pInfo->pPool, szNewName);
                }
            }

        }                       /* End for (period) */

    }                           /* End for (log files */


    return APR_SUCCESS;
}



/*
 * Return the start of a given period, offset by a given number of
 * seconds.  The offset can be negative, which complicates the calculation
 * and forces us to work out how far through the current period we are before
 * reliably being able to return the correct result
 */
static apr_time_t
offset_period_start(int nCount, rotate_interval_t ePeriod,
                    apr_int64_t nOffset)
{
    apr_time_t tStart;

    /* If using a negative offset (eg. monthly minus 1 hour) */
    if (nOffset < 0) {

        /* Get the start of the next period and offset it */
        tStart = period_start(1, ePeriod);
        tStart += nOffset;

        /* If we're already into the "next" period then offset the
         * period count that's been asked for by one */
        if (tStart < apr_time_now()) {
            nCount++;
        }
    }

    tStart = period_start(nCount, ePeriod);
    tStart += nOffset;
    return tStart;
}


/*
 * Return the start of a given period.  nCount defines the number of periods
 * ahead or before now.  For example :
 *  n = 0,   e = DAILY    Midnight today
 *  n = 1,   e = DAILY    Midnight tomorrow
 *  n = 0,   e = MONTHLY  Midnight on 1st day of the current month
 *  n = -1,  e = MONTHLY  Midnight on 1st day of the previous month
 */
static apr_time_t period_start(int nCount, rotate_interval_t ePeriod)
{
    apr_time_exp_t T;
    apr_time_t tNow, tThen;

    /* Representation of now, local time */
    tNow = apr_time_now();
    apr_time_exp_lt(&T, tNow);

    /* If period is monthly or weekly, go to the start of the month or
     * start of the week.
     * In all cases move forward or back the given number of periods.
     */
    switch (ePeriod) {
    case MONTHLY:
        T.tm_mday = 1;          /* Start of this month */
        T.tm_mon += nCount;     /* plus or minus a number of months */
        T.tm_hour = 0;          /* Start at midnight */

        /* Apache's mktime() replacement doesn't normalise
         * out-of-range month fields across year boundaries,
         * so let's just do that ourselves */
        T.tm_year += T.tm_mon / 12;
        T.tm_mon = T.tm_mon % 12;
        if (T.tm_mon < 0) {     /* Deal with past months */
            T.tm_mon += 12;
            T.tm_year -= 1;
        }

        T.tm_min = 0;
        break;
    case WEEKLY:
        T.tm_mday -= T.tm_wday; /* Start of this week */
        T.tm_mday += (7 * nCount);      /* +/- number of weeks */
        T.tm_hour = 0;          /* Start at midnight */
        T.tm_min = 0;
        break;
    case DAILY:
        T.tm_mday += nCount;    /* +/- number of days */
        T.tm_hour = 0;          /* Start at midnight */
        T.tm_min = 0;
        break;
    case HOURLY:
        T.tm_hour += nCount;    /* +/- number of hours */
        T.tm_min = 0;
        break;
#if defined(ENABLE_PERMINUTE)
    case PERMINUTE:
        T.tm_min += nCount;     /* +/- number of minutes */
        break;
#endif
    }


    /* Always start at the beginning of the minute */
    T.tm_sec = T.tm_usec = 0;

    apr_time_exp_gmt_get(&tThen, &T);
    return tThen;
}


/*
 * Callback used to notify us that a compress child died
 * pvData is a pointer to the apr_proc_t
 */
static void compress_cb_func(int nReason, void *pvData, int nStatus)
{
    compress_child_info_t *pChildInfo = (compress_child_info_t *) pvData;

    switch (nReason) {
        /* Unregister the child if its finished */
    case APR_OC_REASON_DEATH:
    case APR_OC_REASON_RESTART:
    case APR_OC_REASON_LOST:
        apr_proc_other_child_unregister(pChildInfo);
        break;

        /* This is probably because we called the unregister above.. */
    case APR_OC_REASON_UNREGISTER:
        return;
        break;
    };


    /* Log the success or failure */
    ap_log_perror(APLOG_MARK, APLOG_NOTICE, OK, pChildInfo->pPool,
                  "mod_autorotate: Compress process %d done: %s  %d left.",
                  pChildInfo->pProc->pid,
                  (nStatus == 0) ? "OK" : "FAILED",
                  pChildInfo->aCompressQueue->nelts);

    run_next_compress_child(pChildInfo);
    return;
}


/*
 * Start a child process and register with Apache so that we recieve
 * notification when it dies.
 */
static apr_status_t run_next_compress_child(compress_child_info_t * pData)
{
    const char *const *env;
    apr_procattr_t *procattr;
    apr_proc_t *pProc;
    apr_status_t rc = APR_SUCCESS;

    AP_DEBUG_ASSERT(pData != NULL);

    apr_pool_t *pPool = pData->pPool;

    /* We're done if there are no items left on the queue.
     * Delete the subpool and return
     */
    if (pData->aCompressQueue->nelts == 0) {
        ap_log_perror(APLOG_MARK, APLOG_INFO, rc, pPool,
                      "mod_autorotate: Done compressing");
        apr_pool_clear(pPool);
        pData->aCompressQueue = NULL;
        pData->szLogPath = NULL;
        return OK;
    }

    /* Otherwise plough on with the compressions */
    char **pszEntries = apr_array_pop(pData->aCompressQueue);
    /* Store name of log being worked on right now */
    pData->szLogPath = *pszEntries;
    AP_DEBUG_ASSERT(pData->szLogPath != NULL);

    const char *argv[] = { pData->szCompressProgram, pData->szLogPath, NULL };

    /* Set up attributes of the process */
    if (((rc = apr_procattr_create(&procattr, pPool)) != APR_SUCCESS) ||
        ((rc = apr_procattr_io_set(procattr,
                                   APR_FULL_BLOCK,
                                   APR_NO_PIPE,
                                   APR_NO_PIPE)) != APR_SUCCESS) ||
        ((rc = apr_procattr_dir_set(procattr, "/tmp")) != APR_SUCCESS) ||
        ((rc = apr_procattr_cmdtype_set(procattr, APR_PROGRAM))
         != APR_SUCCESS) ||
        ((rc = apr_procattr_detach_set(procattr, 0)) != APR_SUCCESS) ||
        ((rc = apr_procattr_addrspace_set(procattr, 0)) != APR_SUCCESS) ||
        ((rc = apr_procattr_error_check_set(procattr, 1)) != APR_SUCCESS)) {

        /* Something bad happened, tell the world. */
        ap_log_perror(APLOG_MARK, APLOG_ERR, rc, pPool,
                      "mod_autorotate: couldn't set child process attributes: %s",
                      pData->szCompressProgram);
    }
    else {

        pProc = apr_pcalloc(pPool, sizeof(*pProc));
        rc = apr_proc_create(pProc, pData->szCompressProgram,
                             argv, NULL, procattr, pPool);

        if (rc != APR_SUCCESS) {
            ap_log_perror(APLOG_MARK, APLOG_ERR, rc, pPool,
                          "mod_autorotate: couldn't create child process: %s",
                          pData->szCompressProgram);
        }
        else {

            /* Renice the process */
            if (setpriority(PRIO_PROCESS, pProc->pid, pData->nNiceLevel)) {
                ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pPool,
                              "mod_autorotate: couldn't set child priority to %d",
                              pData->nNiceLevel);
            }

            /* Register the child with Apache so that we get notified
             * when it dies
             */
            pData->pProc = pProc;
            apr_proc_other_child_register(pProc, compress_cb_func,
                                          pData, pProc->in, pPool);

            /* Associate the child with the global pool so that Apache
             * will kill it when the pool goes out of scope (eg. when the
             * server is killed
             */
            apr_pool_note_subprocess(pPool, pProc, APR_KILL_AFTER_TIMEOUT);

            ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, pPool,
                          "mod_autorotate: Started compress, PID %d, [%s] [%s]",
                          pProc->pid, pData->szCompressProgram,
                          pData->szLogPath);
        }

    }

    return rc;
}


/* ---------  Apache registration  -------------------------------------------*/


/*
 * Register hooks for meddling
 */
static void register_hooks(apr_pool_t * p)
{
    ap_hook_monitor(monitor_func, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_open_logs(open_logs_func, NULL, NULL, APR_HOOK_FIRST);
}



/*
 * Configuration command handlers
 */
static const command_rec autorotate_cmds[] = {
    AP_INIT_FLAG("AutorotateEnabled",
                 cmd_rotate_enabled, NULL,
                 RSRC_CONF,
                 "Enable autorotation"),

    AP_INIT_TAKE1("AutorotatePeriod",
                  cmd_rotate_period, NULL,
                  RSRC_CONF,
                  "The log rotation period (hourly, daily, weekly, monthly)"),

    AP_INIT_TAKE1("AutorotateOffset",
                  cmd_rotate_offset, NULL,
                  RSRC_CONF,
                  "The log rotation offset in seconds, from the beginning of a period"),

    AP_INIT_TAKE1("AutorotateFormat",
                  cmd_rotate_format, NULL,
                  RSRC_CONF,
                  "Format of the rotated log file suffix in strftime(3) format"),

    AP_INIT_TAKE1("AutorotateCompressProgram",
                  cmd_rotate_compress, NULL,
                  RSRC_CONF,
                  "Location of compress binary (default: /usr/bin/gzip)"),

    AP_INIT_TAKE1("AutorotateCompressSuffix",
                  cmd_rotate_compresssuffix, NULL,
                  RSRC_CONF,
                  "Suffix that AutorotateCompressProgram adds to compressed files "
                  " (default: .gz)"),

    AP_INIT_TAKE1("AutorotateCompressNicelevel",
                  cmd_rotate_nicelevel, NULL,
                  RSRC_CONF,
                  "Nice level of child compress processeses (default: 5)"),

    AP_INIT_TAKE1("AutorotateRestartMethod",
                  cmd_rotate_restartmethod, NULL,
                  RSRC_CONF,
                  "Restart method: Full or Graceful (default:  Graceful)"),

    AP_INIT_TAKE1("AutorotateKeep",
                  cmd_rotate_keep, NULL,
                  RSRC_CONF,
                  "Number of log files to keep or 0 to never delete.  (default: 0)"),

    AP_INIT_TAKE1("AutorotateCompressAfter",
                  cmd_rotate_compressafter, NULL,
                  RSRC_CONF,
                  "Compress after this number of rotates or 0 to never compress. "
                  " (default: 1)"),

    AP_INIT_TAKE12("AutorotateAddLogDirective",
                   cmd_add_log_directive, NULL,
                   RSRC_CONF,
                   "Add a directive provided by a third-party module that defines a log file. "
                   "Supply an optional argument position that the log file appears in for that "
                   "directive."),


    AP_INIT_TAKE1("AutorotateAddLogFile",
                  cmd_add_log_file, NULL,
                  RSRC_CONF,
                  "Include a specific log file in the rotation scheme.  This is intended for use "
                  "where modules creates log files that aren't defined by Apache directives "
                  "such as Siteminder logs"),


    {NULL}
};


/*
 * Module descriptor
 */
module AP_MODULE_DECLARE_DATA autorotate_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                       /* create per-directory config func */
    NULL,                       /* merge per-directory config func */
    create_autorotate_config,   /* create per-server config func */
    NULL,                       /* merge per-server config func */
    autorotate_cmds,            /* config directive handlers */
    register_hooks              /* register hooks with Apache framework */
};
