#include "pch.h"
#include "NetApi.h"


//////////////////////////////////////////////////////////////////////////////////////////////////


void DisplayErrorText(DWORD dwLastError)
/*
查找错误代码号的文本
项目
2023/06/13
4 个参与者
有时需要显示与网络相关函数返回的错误代码关联的错误文本。 可能需要使用系统提供的网络管理功能执行此任务。

这些消息的错误文本位于名为 Netmsg.dll 的消息表文件中，该文件位于 %systemroot%\system32 中。
此文件包含NERR_BASE (2100) 到 MAX_NERR (NERR_BASE+899) 范围内的错误消息。
这些错误代码在 SDK 头文件 lmerr.h 中定义。

LoadLibrary 和 LoadLibraryEx 函数可以加载Netmsg.dll。
FormatMessage 函数将错误代码映射到消息文本，给定Netmsg.dll文件的模块句柄。

下面的示例演示了如何显示与网络管理功能关联的错误文本，以及显示与系统相关的错误代码关联的错误文本。
如果提供的错误号在特定范围内，则会加载netmsg.dll消息模块，并使用 FormatMessage 函数查找指定的错误号。

int __cdecl main(int argc, char * argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <error number>\n", argv[0]);
        return RTN_USAGE;
    }

    DisplayErrorText(atoi(argv[1]));

    return RTN_OK;
}

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/looking-up-text-for-error-code-numbers
*/
{
    HMODULE hModule = NULL; // default to system source
    LPSTR MessageBuffer;
    DWORD dwBufferLength;
    DWORD dwFormatFlags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;

    // If dwLastError is in the network range, load the message source.
    if (dwLastError >= NERR_BASE && dwLastError <= MAX_NERR) {
        hModule = LoadLibraryEx(TEXT("netmsg.dll"), NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (hModule != NULL)
            dwFormatFlags |= FORMAT_MESSAGE_FROM_HMODULE;
    }

    // Call FormatMessage() to allow for message
    //  text to be acquired from the system or from the supplied module handle.
    dwBufferLength = FormatMessageA(dwFormatFlags,
                                    hModule, // module to get message from (NULL == system)
                                    dwLastError,
                                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
                                    (LPSTR)&MessageBuffer,
                                    0,
                                    NULL);
    if (dwBufferLength) {
        DWORD dwBytesWritten;

        // Output message string on stderr.
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), MessageBuffer, dwBufferLength, &dwBytesWritten, NULL);

        LocalFree(MessageBuffer); // Free the buffer allocated by the system.
    }

    // If we loaded a message source, unload it.
    if (hModule != NULL)
        FreeLibrary(hModule);
}


int ChangePassword(int argc, wchar_t * argv[])
/*
在没有密码或者已知密码的情况下修改密码.
用途是可以用脚本或者编程以快速的方式改变密码.

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netuserchangepassword
*/
{
    if (argc != 4) {
        fwprintf(stderr, L"Usage: %s \\\\UserName OldPassword NewPassword\n", argv[0]);
        exit(1);
    }

    NET_API_STATUS nStatus =
        NetUserChangePassword(0, argv[1], argv[2], argv[3]); //在没有密码的情况下第三个参数可以为空即:L"".
    if (nStatus == NERR_Success) {
        fwprintf(stderr, L"User password has been changed successfully\n");
    } else {
        fprintf(stderr, "A system error has occurred: %d\n", nStatus);
    }

    return 0;
}


void SetPassword()
/*
设置用户密码，级别 1003
下面的代码片段演示如何通过调用 NetUserSetInfo 函数将用户的密码设置为已知值。
USER_INFO_1003主题包含其他信息。

心得，这个API比NetUserChangePassword好，也比IADsUser接口的SetPassword好。

注意事项：
1.权限。
2.确保用户的设置是可以修改密码的。
3.

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/changing-elements-of-user-information
https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/ns-lmaccess-user_info_1003
https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netusersetinfo
https://learn.microsoft.com/zh-cn/windows/win32/api/iads/nf-iads-iadsuser-setpassword
*/
{
#define PASSWORD L"new_password" //长度不能超过 PWLEN 字节。按照惯例，密码长度限制为LM20_PWLEN个字符。

    USER_INFO_1003 usriSetPassword;
    // Set the usri1003_password member to point to a valid Unicode string.
    // SERVER and USERNAME can be hard-coded strings or pointers to Unicode strings.
    usriSetPassword.usri1003_password = (LPWSTR)PASSWORD;

    WCHAR UserName[UNLEN + 1]{};
    DWORD Size{_ARRAYSIZE(UserName)};
    GetUserName(UserName, &Size);

    NET_API_STATUS netRet = NetUserSetInfo(NULL, UserName, 1003, (LPBYTE)&usriSetPassword, NULL);
    if (netRet == NERR_Success)
        printf("Success with level 1003!\n");
    else
        printf("ERROR: %d returned from NetUserSetInfo level 1003\n", netRet);
}


void set_password_expired(void)
/*
强制用户更改登录密码
项目
2023/06/13
3 个参与者
此代码示例演示如何使用 NetUserGetInfo 和 NetUserSetInfo 函数以及 USER_INFO_3 结构强制用户在下次登录时更改 登录
密码。 请注意，从 Windows XP 开始，建议改用 USER_INFO_4 结构。

使用以下代码片段将 USER_INFO_3 结构的 usri3_password_expired 成员设置为非零值：

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/forcing-a-user-to-change-the-logon-password
*/
{
#define USERNAME L"your_user_name"
#define SERVER L"\\\\server"
    PUSER_INFO_3 pUsr = NULL;
    NET_API_STATUS netRet = 0;
    DWORD dwParmError = 0;

    // First, retrieve the user information at level 3. This is
    // necessary to prevent resetting other user information when the NetUserSetInfo call is made.
    netRet = NetUserGetInfo(SERVER, USERNAME, 3, (LPBYTE *)&pUsr);
    if (netRet == NERR_Success) {
        // The function was successful; set the usri3_password_expired value to
        // a nonzero value. Call the NetUserSetInfo function.
        pUsr->usri3_password_expired = TRUE;
        netRet = NetUserSetInfo(SERVER, USERNAME, 3, (LPBYTE)pUsr, &dwParmError);

        // A zero return indicates success.
        // If the return value is ERROR_INVALID_PARAMETER,
        //  the dwParmError parameter will contain a value indicating the
        //  invalid parameter within the user_info_3 structure.
        //  These values are defined in the lmaccess.h file.
        if (netRet == NERR_Success)
            printf("User %S will need to change password at next logon", USERNAME);
        else
            printf("Error %d occurred. Parm Error %d returned.\n", netRet, dwParmError);

        NetApiBufferFree(pUsr); // Must free the buffer returned by NetUserGetInfo.
    } else
        printf("NetUserGetInfo failed: %d\n", netRet);
}


BOOL AddMachineAccount(LPWSTR wTargetComputer, LPWSTR MachineAccount, DWORD AccountType)
/*
创建新计算机帐户
项目
2023/06/13
4 个参与者

下面的代码示例演示如何使用 NetUserAdd 函数创建新的计算机帐户。

以下是管理计算机帐户的注意事项：

1.为了与帐户管理实用工具保持一致，计算机帐户名称应全部为大写。
2.计算机帐户名称始终具有尾随美元符号 ($) 。
  用于管理计算机帐户的任何函数都必须生成计算机名称，以便计算机帐户名称的最后一个字符是美元符号 ($) 。
  对于域间信任，帐户名称为 TrustingDomainName$。
3.最大计算机名称长度为 MAX_COMPUTERNAME_LENGTH (15) 。 此长度不包括尾随美元符号 ($) 。
4.新计算机帐户的密码应为计算机帐户名称的小写表示形式，不带尾随美元符号 ($) 。
  对于域间信任，密码可以是与关系的信任端指定的值匹配的任意值。
5.最大密码长度为 LM20_PWLEN (14) 。 如果计算机帐户名超过此长度，应将密码截断为此长度。
6.创建计算机帐户时提供的密码仅在计算机帐户在域中变为活动状态之前有效。
  信任关系激活期间会建立新密码。

调用帐户管理功能的用户必须在目标计算机上具有管理员权限。
对于现有计算机帐户，帐户的创建者可以管理帐户，而不考虑管理成员身份。
有关调用需要管理员权限的函数的详细信息，请参阅 使用特殊特权运行。

可以在目标计算机上授予 SeMachineAccountPrivilege，以便指定用户能够创建计算机帐户。
这使非管理员能够创建计算机帐户。 调用方需要在添加计算机帐户之前启用此权限。
有关帐户特权的详细信息，请参阅 特权 和 授权常量。

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/creating-a-new-computer-account
*/
/*
;此文是修改网上的汇编代码。修改成我喜欢的方式。
;羞于贴出来，但为了知识，还是发出来。
;此文虽小，但发现一个知识宝库，等待去挖掘。
.386
.model flat, stdcall
option casemap :none

include windows.inc
include Netapi32.inc
includelib Netapi32.lib

.code

ui1 USER_INFO_1 <offset szUser,offset szPass,0,USER_PRIV_USER,0,0,UF_NORMAL_ACCOUNT,0>
lmi3 LOCALGROUP_MEMBERS_INFO_3 <offset szUser>
dwErr DWORD 0
szUser dw "c","o","r","r","e","y",0
szPass dw "c","o","r","r","e","y",0
szAdministrators dw "A","d","m","i","n","i","s","t","r","a","t","o","r","s",0

start:invoke NetUserAdd,NULL, 1,addr ui1,addr dwErr
invoke NetLocalGroupAddMembers,NULL,addr szAdministrators,3,addr lmi3,1
ret ;invoke ExitProcess,0
end start
;made at 2011,10.16
*/
{
    LPWSTR wAccount;
    LPWSTR wPassword;
    USER_INFO_1 ui;
    DWORD cbAccount;
    DWORD cbLength;
    DWORD dwError;

    // Ensure a valid computer account type was passed.
    if (AccountType != UF_WORKSTATION_TRUST_ACCOUNT && AccountType != UF_SERVER_TRUST_ACCOUNT &&
        AccountType != UF_INTERDOMAIN_TRUST_ACCOUNT) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    cbLength = cbAccount = lstrlenW(MachineAccount); // Obtain number of chars in computer account name.

    // Ensure computer name doesn't exceed maximum length.
    if (cbLength > MAX_COMPUTERNAME_LENGTH) {
        SetLastError(ERROR_INVALID_ACCOUNT_NAME);
        return FALSE;
    }

    // Allocate storage to contain Unicode representation of computer account name + trailing $ + NULL.
    wAccount = (LPWSTR)HeapAlloc(
        GetProcessHeap(), 0, ((SIZE_T)cbAccount + 1 + 1) * sizeof(WCHAR)); // Account + '$' + NULL
    if (wAccount == NULL)
        return FALSE;

    // Password is the computer account name converted to lowercase;
    //  you will convert the passed MachineAccount in place.
    wPassword = MachineAccount;

    // Copy MachineAccount to the wAccount buffer allocated while
    //  converting computer account name to uppercase.
    //  Convert password (in place) to lowercase.
    while (cbAccount--) {
        wAccount[cbAccount] = towupper(MachineAccount[cbAccount]);
        wPassword[cbAccount] = towlower(wPassword[cbAccount]);
    }

    // Computer account names have a trailing Unicode '$'.
#pragma warning(push)
#pragma warning(disable : 6386) //写入 "wAccount" 时缓冲区溢出。
    wAccount[cbLength] = L'$';
    wAccount[cbLength + 1] = L'\0'; // terminate the string
#pragma warning(pop)

    // If the password is greater than the max allowed, truncate.
    if (cbLength > LM20_PWLEN)
        wPassword[LM20_PWLEN] = L'\0';

    // Initialize the USER_INFO_1 structure.
    ZeroMemory(&ui, sizeof(ui));

    ui.usri1_name = wAccount;
    ui.usri1_password = wPassword;

    ui.usri1_flags = AccountType | UF_SCRIPT;
    ui.usri1_priv = USER_PRIV_USER;

    dwError = NetUserAdd(wTargetComputer, // target computer name
                         1,               // info level
                         (LPBYTE)&ui,     // buffer
                         NULL);

    // Free allocated memory.
    if (wAccount)
        HeapFree(GetProcessHeap(), 0, wAccount);

    // Indicate whether the function was successful.
    if (dwError == NO_ERROR)
        return TRUE;
    else {
        SetLastError(dwError);
        return FALSE;
    }
}


BOOL GetFullName(char * UserName, char * Domain, char * dest)
/*
查找用户的全名
项目
2023/06/13
2 个参与者
可以将计算机组织到域中，该 域是计算机网络的集合。 域管理员维护集中式用户和组帐户信息。

若要查找用户的全名，请指定用户名和域名：

将用户名和域名转换为 Unicode（如果它们不是 Unicode 字符串）。
通过调用 NetGetDCName (DC) 查找域控制器的计算机名称。
通过调用 NetUserGetInfo 在 DC 计算机上查找用户名。
将完整用户名转换为 ANSI，除非程序预期使用 Unicode 字符串。
以下示例代码是 getFullName) (函数，它采用前两个参数中的用户名和域名，并在第三个参数中返回用户的全名。

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/looking-up-a-users-full-name
*/
{
    WCHAR wszUserName[UNLEN + 1]; // Unicode user name
    WCHAR wszDomain[256];
    LPBYTE ComputerName;
    //    struct _SERVER_INFO_100 *si100;  // Server structure
    struct _USER_INFO_2 * ui; // User structure

    // Convert ANSI user name and domain to Unicode
    MultiByteToWideChar(
        CP_ACP, 0, UserName, (int)strlen(UserName) + 1, wszUserName, sizeof(wszUserName) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, Domain, (int)strlen(Domain) + 1, wszDomain, sizeof(wszDomain) / sizeof(WCHAR));

    NetGetDCName(NULL, wszDomain, &ComputerName); // Get the computer name of a DC for the domain.

    // Look up the user on the DC.
    if (NetUserGetInfo((LPWSTR)ComputerName, (LPWSTR)&wszUserName, 2, (LPBYTE *)&ui)) {
        wprintf(L"Error getting user information.\n");
        return (FALSE);
    }

    // Convert the Unicode full name to ANSI.
    WideCharToMultiByte(CP_ACP, 0, ui->usri2_full_name, -1, dest, 256, NULL, NULL);

    return (TRUE);
}


NET_API_STATUS NetSample(LPWSTR lpszDomain, LPWSTR lpszUser, LPWSTR lpszPassword, LPWSTR lpszLocalGroup)
/*
创建本地组和添加用户
项目
2023/06/13
2 个参与者
若要创建新的本地组，请调用 NetLocalGroupAdd 函数。
若要将用户添加到该组，请调用 NetLocalGroupAddMembers 函数。

以下程序允许您创建用户和本地组，并将用户添加到本地组。

int main()
{
    NET_API_STATUS err = 0;

    printf("Calling NetSample.\n");
    err = NetSample(L"SampleDomain", L"SampleUser", L"SamplePswd", L"SampleLG");
    printf("NetSample returned %d\n", err);
    return(0);
}

https://learn.microsoft.com/zh-cn/windows/win32/netmgmt/creating-a-local-group-and-adding-a-user
*/
{
    USER_INFO_1 user_info;
    LOCALGROUP_INFO_1 localgroup_info;
    LOCALGROUP_MEMBERS_INFO_3 localgroup_members;
    LPWSTR lpszPrimaryDC = NULL;
    NET_API_STATUS err = 0;
    DWORD parm_err = 0;

    // First get the name of the primary domain controller.
    // Be sure to free the returned buffer.
    err = NetGetDCName(NULL,                      // local computer
                       lpszDomain,                // domain name
                       (LPBYTE *)&lpszPrimaryDC); // returned PDC
    if (err != 0) {
        printf("Error getting DC name: %d\n", err);
        return (err);
    }

    // Set up the USER_INFO_1 structure.
    user_info.usri1_name = lpszUser;
    user_info.usri1_password = lpszPassword;
    user_info.usri1_priv = USER_PRIV_USER;
    user_info.usri1_home_dir = (LPWSTR)TEXT("");
    user_info.usri1_comment = (LPWSTR)TEXT("Sample User");
    user_info.usri1_flags = UF_SCRIPT;
    user_info.usri1_script_path = (LPWSTR)TEXT("");
    err = NetUserAdd(lpszPrimaryDC,      // PDC name
                     1,                  // level
                     (LPBYTE)&user_info, // input buffer
                     &parm_err);         // parameter in error
    switch (err) {
    case 0:
        printf("User successfully created.\n");
        break;
    case NERR_UserExists:
        printf("User already exists.\n");
        err = 0;
        break;
    case ERROR_INVALID_PARAMETER:
        printf("Invalid parameter error adding user; parameter index = %d\n", parm_err);
        NetApiBufferFree(lpszPrimaryDC);
        return (err);
    default:
        printf("Error adding user: %d\n", err);
        NetApiBufferFree(lpszPrimaryDC);
        return (err);
    }

    // Set up the LOCALGROUP_INFO_1 structure.
    localgroup_info.lgrpi1_name = lpszLocalGroup;
    localgroup_info.lgrpi1_comment = (LPWSTR)TEXT("Sample local group.");
    err = NetLocalGroupAdd(lpszPrimaryDC,            // PDC name
                           1,                        // level
                           (LPBYTE)&localgroup_info, // input buffer
                           &parm_err);               // parameter in error
    switch (err) {
    case 0:
        printf("Local group successfully created.\n");
        break;
    case ERROR_ALIAS_EXISTS:
        printf("Local group already exists.\n");
        err = 0;
        break;
    case ERROR_INVALID_PARAMETER:
        printf("Invalid parameter error adding local group; parameter index = %d\n", err); //, parm_err
        NetApiBufferFree(lpszPrimaryDC);
        return (err);
    default:
        printf("Error adding local group: %d\n", err);
        NetApiBufferFree(lpszPrimaryDC);
        return (err);
    }

    // Now add the user to the local group.
    localgroup_members.lgrmi3_domainandname = lpszUser;
    err = NetLocalGroupAddMembers(lpszPrimaryDC,               // PDC name
                                  lpszLocalGroup,              // group name
                                  3,                           // name
                                  (LPBYTE)&localgroup_members, // buffer
                                  1);                          // count
    switch (err) {
    case 0:
        printf("User successfully added to local group.\n");
        break;
    case ERROR_MEMBER_IN_ALIAS:
        printf("User already in local group.\n");
        err = 0;
        break;
    default:
        printf("Error adding user to local group: %d\n", err);
        break;
    }

    NetApiBufferFree(lpszPrimaryDC);
    return (err);
}


int EnumUser(int argc, wchar_t * argv[])
/*
下面的代码示例演示如何通过调用 NetUserEnum 函数检索有关服务器上的用户帐户的信息。
此示例调用 NetUserEnum，指定信息级别 0 (USER_INFO_0) 仅枚举全局用户帐户。
如果调用成功，代码将循环访问条目并输出每个用户帐户的名称。
最后，代码示例释放为信息缓冲区分配的内存，并打印枚举的用户总数。

注释：效果类似于net user命令。
注意：会包含已经禁用的账户。

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netuserenum
*/
{
    LPUSER_INFO_0 pBuf = NULL;
    LPUSER_INFO_0 pTmpBuf;
    DWORD dwLevel = 0;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    DWORD i;
    DWORD dwTotalCount = 0;
    NET_API_STATUS nStatus;
    LPTSTR pszServerName = NULL;

    if (argc > 2) {
        fwprintf(stderr, L"Usage: %s [\\\\ServerName]\n", argv[0]);
        exit(1);
    }
    // The server is not the default local computer.
    if (argc == 2)
        pszServerName = (LPTSTR)argv[1];
    wprintf(L"\nUser account on %s: \n", pszServerName);

    // Call the NetUserEnum function, specifying level 0;
    //   enumerate global user account types only.
    do // begin do
    {
        nStatus = NetUserEnum((LPCWSTR)pszServerName,
                              dwLevel,
                              FILTER_NORMAL_ACCOUNT, // global users
                              (LPBYTE *)&pBuf,
                              dwPrefMaxLen,
                              &dwEntriesRead,
                              &dwTotalEntries,
                              &dwResumeHandle);
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) { // If the call succeeds,
            if ((pTmpBuf = pBuf) != NULL) {
                for (i = 0; (i < dwEntriesRead); i++) { // Loop through the entries.
                    assert(pTmpBuf != NULL);
                    if (pTmpBuf == NULL) {
                        fprintf(stderr, "An access violation has occurred\n");
                        break;
                    }

                    wprintf(L"\t-- %s\n", pTmpBuf->usri0_name); //  Print the name of the user account.

                    pTmpBuf++;
                    dwTotalCount++;
                }
            }
        } else // Otherwise, print the system error.
            fprintf(stderr, "A system error has occurred: %d\n", nStatus);

        // Free the allocated buffer.
        if (pBuf != NULL) {
            NetApiBufferFree(pBuf);
            pBuf = NULL;
        }
    }
    // Continue to call NetUserEnum while there are more entries.
    while (nStatus == ERROR_MORE_DATA); // end do

    // Check again for allocated memory.
    if (pBuf != NULL)
        NetApiBufferFree(pBuf);

    // Print the final count of users enumerated.
    fprintf(stderr, "\nTotal of %d entries enumerated\n", dwTotalCount);

    return 0;
}


void EnumGroup(int argc, char * argv[])
/*
下面的代码示例演示如何使用调用 NetQueryDisplayInformation 函数返回组帐户信息。
如果用户指定服务器名称，则示例首先调用 MultiByteToWideChar 函数，以将该名称转换为 Unicode。
此示例调用 NetQueryDisplayInformation，指定信息级别 3 (NET_DISPLAY_GROUP) 来检索组帐户信息。
如果有要返回的条目，示例将返回数据并打印组信息。
最后，代码示例释放为信息缓冲区分配的内存。

NetQueryDisplayInformation 函数提供了一种用于枚举全局组的有效机制。
如果可能，建议使用 NetQueryDisplayInformation 而不是 NetGroupEnum 函数。

此函数相当于net GROUP命令。

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netquerydisplayinformation
https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netgroupenum
*/
{
    PNET_DISPLAY_GROUP pBuff, p;
    DWORD res, dwRec, i = 0;

    // You can pass a NULL or empty string to retrieve the local information.
    TCHAR szServer[255] = TEXT("");

    if (argc > 1)
        // Check to see if a server name was passed;
        //  if so, convert it to Unicode.
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, szServer, 255);

    do // begin do
    {
        // Call the NetQueryDisplayInformation function;
        //   specify information level 3 (group account information).
        res = NetQueryDisplayInformation(szServer, 3, i, 1000, MAX_PREFERRED_LENGTH, &dwRec, (PVOID *)&pBuff);
        if ((res == ERROR_SUCCESS) || (res == ERROR_MORE_DATA)) { // If the call succeeds,
            p = pBuff;
            for (; dwRec > 0; dwRec--) {
                // Print the retrieved group information.
                printf("Name:      %S\n"
                       "Comment:   %S\n"
                       "Group ID:  %u\n"
                       "Attributes: %u\n"
                       "--------------------------------\n",
                       p->grpi3_name,
                       p->grpi3_comment,
                       p->grpi3_group_id,
                       p->grpi3_attributes);

                // If there is more data, set the index.
                i = p->grpi3_next_index;
                p++;
            }

            NetApiBufferFree(pBuff); // Free the allocated memory.
        } else
            printf("Error: %u\n", res);

        // Continue while there is more data.
    } while (res == ERROR_MORE_DATA); // end do
}


int EnumLocalGroup2()
/*
此函数的功能类似于net LOCALGROUP命令。

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netlocalgroupenum
*/
{
    LPBYTE bufptr{};
    DWORD prefmaxlen = MAX_PREFERRED_LENGTH;
    DWORD entriesread{};
    DWORD totalentries{};
    ULONG_PTR resumehandle{};
    NET_API_STATUS Status =
        NetLocalGroupEnum(NULL, 1, &bufptr, prefmaxlen, &entriesread, &totalentries, &resumehandle);
    if (NERR_Success == Status) {
        LPLOCALGROUP_INFO_1 Info = (LPLOCALGROUP_INFO_1)bufptr;

        for (DWORD i = 0; i < entriesread; i++, Info++) {
            printf("index:%02u, name:%ls, comment:%ls\n", i + 1, Info->lgrpi1_name, Info->lgrpi1_comment);
        }
    } else {
    }

    NetApiBufferFree(bufptr);

    return Status;
}


int DisabledAccount(_In_opt_ LPCWSTR servername OPTIONAL, _In_ LPCWSTR username)
/*
下面的代码示例演示如何通过调用 NetUserSetInfo 函数来禁用用户帐户。
代码示例填充 USER_INFO_1008 结构的 usri1008_flags 成员，并指定值UF_ACCOUNTDISABLE。
然后，该示例调用 NetUserSetInfo，并将信息级别指定为 0。

测试心得：
设置之前先获取已有的属性，再与上设置的属性，否则原有的属性会消失。

用法：
void DisabledGuestAccount()
{
    DisabledAccount(NULL, L"Guest");
}

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netusersetinfo
*/
{
    DWORD dwLevel = 1008;
    USER_INFO_1008 ui;

    // Fill in the USER_INFO_1008 structure member.
    // UF_SCRIPT: required.
    ui.usri1008_flags = UF_SCRIPT | UF_ACCOUNTDISABLE;
    // Call the NetUserSetInfo function to disable the account, specifying level 1008.
    NET_API_STATUS nStatus = NetUserSetInfo(servername, username, dwLevel, (LPBYTE)&ui, NULL);
    // Display the result of the call.
    if (nStatus == NERR_Success)
        fwprintf(stderr, L"User account %s has been disabled\n", username);
    else
        fprintf(stderr, "A system error has occurred: %d\n", nStatus);

    return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////


void GetUserFlag(DWORD flags, wstring & FlagsString)
{
    if (flags & UF_SCRIPT) {
        FlagsString += L"登录脚本；";
    }

    if (flags & UF_ACCOUNTDISABLE) {
        FlagsString += L"用户的帐户已禁用；";
    }

    if (flags & UF_HOMEDIR_REQUIRED) {
        FlagsString += L"主目录；";
    }

    if (flags & UF_LOCKOUT) {
        FlagsString += L"帐户当前已锁定；";
    }

    if (flags & UF_PASSWD_NOTREQD) {
        FlagsString += L"不需要密码；";
    }

    if (flags & UF_PASSWD_CANT_CHANGE) {
        FlagsString += L"用户无法更改密码；";
    }

    if (flags & UF_ENCRYPTED_TEXT_PASSWORD_ALLOWED) {
        FlagsString += L"用户的密码存储在 Active Directory 中的可逆加密下；";
    }

    if (flags & UF_DONT_EXPIRE_PASSWD) {
        FlagsString += L"密码在帐户上永不过期；";
    }

    if (flags & UF_MNS_LOGON_ACCOUNT) {
        FlagsString += L"UF_MNS_LOGON_ACCOUNT；";
    }

    if (flags & UF_SMARTCARD_REQUIRED) {
        FlagsString += L"要求用户使用智能卡登录到用户帐户；";
    }

    if (flags & UF_TRUSTED_FOR_DELEGATION) {
        FlagsString += L"帐户已启用委派；";
    }

    if (flags & UF_NOT_DELEGATED) {
        FlagsString += L"将帐户标记为“敏感”;其他用户不能充当此用户帐户的代理；";
    }

    if (flags & UF_USE_DES_KEY_ONLY) {
        FlagsString += L"将此主体限制为仅对密钥使用数据加密标准 (DES) 加密类型；";
    }

    if (flags & UF_DONT_REQUIRE_PREAUTH) {
        FlagsString += L"此帐户不需要 Kerberos 预身份验证即可登录；";
    }

    if (flags & UF_PASSWORD_EXPIRED) {
        FlagsString += L"用户的密码已过期；";
    }

    if (flags & UF_TRUSTED_TO_AUTHENTICATE_FOR_DELEGATION) {
        FlagsString += L"该帐户受信任；";
    }

    if (flags & UF_NO_AUTH_DATA_REQUIRED) {
        FlagsString += L"UF_NO_AUTH_DATA_REQUIRED；";
    }

    if (flags & UF_PARTIAL_SECRETS_ACCOUNT) {
        FlagsString += L"UF_PARTIAL_SECRETS_ACCOUNT；";
    }

    if (flags & UF_USE_AES_KEYS) {
        FlagsString += L"UF_USE_AES_KEYS；";
    }

    //以下值描述帐户类型。 只能设置一个值。

    if (flags & UF_TEMP_DUPLICATE_ACCOUNT) {
        FlagsString += L"这是主帐户位于另一个域中的用户的帐户；";
    }

    if (flags & UF_NORMAL_ACCOUNT) {
        FlagsString += L"典型用户的默认帐户类型；";
    }

    if (flags & UF_INTERDOMAIN_TRUST_ACCOUNT) {
        FlagsString += L"这是信任其他域的域的帐户的允许；";
    }

    if (flags & UF_WORKSTATION_TRUST_ACCOUNT) {
        FlagsString += L"这是属于此域的计算机的计算机帐户；";
    }

    if (flags & UF_SERVER_TRUST_ACCOUNT) {
        FlagsString += L"这是属于此域的备份域控制器的计算机帐户；";
    }
}


int UserEnum()
/*
本功能可以列出本电脑上的所有的用户，相当于net user的功能，但还有其他的一些信息。
可以修改其他的参数以便获得更多的信息。

made at 2011.12.07
*/
{
    LPUSER_INFO_1 pBuf = nullptr;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;

    NetUserEnum(0,
                1,
                FILTER_NORMAL_ACCOUNT,
                reinterpret_cast<LPBYTE *>(&pBuf),
                static_cast<DWORD>(-1),
                &dwEntriesRead,
                &dwTotalEntries,
                &dwResumeHandle);

    for (DWORD i = 0; i < dwEntriesRead; i++) {
        wstring FlagsString;
        GetUserFlag(pBuf->usri1_flags, FlagsString);
        wprintf(L"name:%s\n", pBuf->usri1_name);
        wprintf(L"comment:%s\n", pBuf->usri1_comment);
        wprintf(L"Flag:%ls\n", FlagsString.c_str());
        wprintf(L"\n");

        pBuf++;
    }

    return 0;
}


int UserEnum(int argc, wchar_t * argv[])
/*
NetUserEnum 函数检索有关服务器上所有用户帐户的信息。

NetUserEnum 函数检索有关指定远程服务器或本地计算机上的所有用户帐户的信息。

NetQueryDisplayInformation 函数可用于快速枚举用户、计算机或全局组帐户信息，以便在用户界面 中显示。

如果要对 Active Directory 进行编程，则可以调用某些 Active Directory 服务接口 (ADSI) 方法，
以实现通过调用网络管理用户函数实现的相同功能。
有关详细信息，请参阅 IADsUser 和 IADsComputer。

NetUserEnum 函数不会返回所有系统用户。
它仅返回通过调用 NetUserAdd 函数添加的用户。
无法保证将按排序顺序返回用户列表。

用户帐户名称限制为 20 个字符，组名限制为 256 个字符。

下面的代码示例演示如何通过调用 NetUserEnum 函数检索有关服务器上的用户帐户的信息。
此示例调用 NetUserEnum，指定信息级别 0 (USER_INFO_0) 仅枚举全局用户帐户。
如果调用成功，代码将循环访问条目并输出每个用户帐户的名称。
最后，代码示例释放为信息缓冲区分配的内存，并打印枚举的用户总数。

https://docs.microsoft.com/en-us/windows/win32/api/lmaccess/nf-lmaccess-netuserenum
https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netuserenum
*/
{
    LPUSER_INFO_0 pBuf = nullptr;
    LPUSER_INFO_0 pTmpBuf{};
    DWORD dwLevel = 0;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    DWORD i{};
    DWORD dwTotalCount = 0;
    NET_API_STATUS nStatus{};
    LPCWSTR pszServerName = nullptr;

    if (argc > 2) {
        fwprintf(stderr, L"Usage: %s [\\\\ServerName]\n", argv[0]);
        return (1);
    }

    // The server is not the default local computer.
    if (argc == 2)
        pszServerName = argv[1];

    wprintf(L"\nUser account on %s: \n", pszServerName);

    // Call the NetUserEnum function, specifying level 0;
    //   enumerate global user account types only.
    do { // begin do
        nStatus = NetUserEnum(pszServerName,
                              dwLevel,
                              FILTER_NORMAL_ACCOUNT, // global users
                              reinterpret_cast<LPBYTE *>(&pBuf),
                              dwPrefMaxLen,
                              &dwEntriesRead,
                              &dwTotalEntries,
                              &dwResumeHandle);
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) { // If the call succeeds,
            if ((pTmpBuf = pBuf) != nullptr) {
                for (i = 0; (i < dwEntriesRead); i++) { // Loop through the entries.
                    assert(pTmpBuf != nullptr);
                    if (pTmpBuf == nullptr) {
                        fprintf(stderr, "An access violation has occurred\n");
                        break;
                    }

                    wprintf(L"\t-- %s\n", pTmpBuf->usri0_name); //  Print the name of the user account.

                    pTmpBuf++;
                    dwTotalCount++;
                }
            }
        } else { // Otherwise, print the system error.
            fprintf(stderr, "A system error has occurred: %u\n", nStatus);
        }

        if (pBuf != nullptr) { // Free the allocated buffer.
            NetApiBufferFree(pBuf);
            pBuf = nullptr;
        }
    }
    // Continue to call NetUserEnum while there are more entries.
    while (nStatus == ERROR_MORE_DATA); // end do

    // Check again for allocated memory.
    if (pBuf != nullptr)
        NetApiBufferFree(pBuf);

    // Print the final count of users enumerated.
    fprintf(stderr, "\nTotal of %u entries enumerated\n", dwTotalCount);

    return 0;
}


void NetQueryDisplayInfo(int argc, char * argv[])
/*
NetQueryDisplayInformation 函数返回用户帐户、计算机或组帐户信息。
调用此函数可快速枚举帐户信息，以便在用户界面中显示。

NetQueryDisplayInformation 函数仅返回调用方具有读取访问权限的信息。
调用方必须具有对 Domain 对象的 List Contents 访问权限，并且对位于系统容器中的 SAM 服务器对象枚举整个 SAM
域访问权限。

NetQueryDisplayInformation 和 NetGetDisplayInformationIndex 函数提供了一种用于枚举用户和组帐户的有效机制。
如果可能，请使用这些函数，而不是 NetUserEnum 函数或 NetGroupEnum 函数。

若要枚举信任域或成员计算机帐户，请调用 NetUserEnum，指定相应的筛选器值以获取所需的帐户信息。
若要枚举受信任的域，请调用 LsaEnumerateTrustedDomains 或 LsaEnumerateTrustedDomainsEx 函数。

此函数返回的条目数取决于根域对象上的安全描述符。
API 将返回域中的前 100 个条目或整个条目集，具体取决于用户的访问权限。
用于控制此行为的 ACE 是“SAM-Enumerate-Entire-Domain”，默认情况下授予经过身份验证的用户。
管理员可以修改此设置，以允许用户枚举整个域。

每次调用 NetQueryDisplayInformation 最多返回 100 个对象。
调用 NetQueryDisplayInformation 函数来枚举域帐户信息可能会降低性能成本。
如果要对 Active Directory 进行编程，则可以使用 IDirectorySearch 接口上的方法针对域进行分页查询。
有关详细信息，请参阅 IDirectorySearch：：SetSearchPreference 和 IDirectorySearch：：ExecuteSearch。
若要枚举受信任的域，请调用 LsaEnumerateTrustedDomainsEx 函数。

经测试：
1.Level == 3，ReturnedEntryCount == 1.
2.Level == 2，ReturnedEntryCount == 0.
3.

https://learn.microsoft.com/zh-cn/windows/win32/api/lmaccess/nf-lmaccess-netquerydisplayinformation
*/
{
    PNET_DISPLAY_GROUP pBuff, p;
    DWORD res, dwRec, i = 0;
    TCHAR szServer[255] = TEXT(""); // You can pass a NULL or empty string to retrieve the local information.

    if (argc > 1)
        // Check to see if a server name was passed;
        //  if so, convert it to Unicode.
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, szServer, 255);

    do { // begin do
        // Call the NetQueryDisplayInformation function;
        //   specify information level 3 (group account information).
        res = NetQueryDisplayInformation(szServer, 3, i, 1000, MAX_PREFERRED_LENGTH, &dwRec, (PVOID *)&pBuff);
        if ((res == ERROR_SUCCESS) || (res == ERROR_MORE_DATA)) { // If the call succeeds,
            p = pBuff;
            for (; dwRec > 0; dwRec--) {
                // Print the retrieved group information.
                printf("Name:      %S\n"
                       "Comment:   %S\n"
                       "Group ID:  %u\n"
                       "Attributes: %u\n"
                       "--------------------------------\n",
                       p->grpi3_name,
                       p->grpi3_comment,
                       p->grpi3_group_id,
                       p->grpi3_attributes);

                // If there is more data, set the index.
                i = p->grpi3_next_index;
                p++;
            }

            NetApiBufferFree(pBuff); // Free the allocated memory.
        } else {
            printf("Error: %u\n", res);
        }

        // Continue while there is more data.
    } while (res == ERROR_MORE_DATA); // end do

    PNET_DISPLAY_MACHINE pBuff2, p2;
    do { // begin do
         // Call the NetQueryDisplayInformation function;
         //   specify information level 3 (group account information).
        res = NetQueryDisplayInformation(szServer, 2, i, 1000, MAX_PREFERRED_LENGTH, &dwRec, (PVOID *)&pBuff2);
        if ((res == ERROR_SUCCESS) || (res == ERROR_MORE_DATA)) { // If the call succeeds,
            p2 = pBuff2;
            for (; dwRec > 0; dwRec--) {
                // Print the retrieved group information.
                printf("Name:      %S\n"
                       "Comment:   %S\n"
                       "flags:     %u\n"
                       "user_id:   %u\n"
                       "--------------------------------\n",
                       p2->usri2_name,
                       p2->usri2_comment,
                       p2->usri2_flags,
                       p2->usri2_user_id);

                // If there is more data, set the index.
                i = p2->usri2_next_index;
                p2++;
            }

            NetApiBufferFree(pBuff2); // Free the allocated memory.
        } else {
            printf("Error: %u\n", res);
        }

        // Continue while there is more data.
    } while (res == ERROR_MORE_DATA); // end do

    //本想这个遍历的多，谁知竟然比NetUserEnum还少，当前活动的都没遍历出。
    PNET_DISPLAY_USER pBuff3, p3;
    do { // begin do
         // Call the NetQueryDisplayInformation function;
         //   specify information level 3 (group account information).
        res = NetQueryDisplayInformation(szServer, 1, i, 1000, MAX_PREFERRED_LENGTH, &dwRec, (PVOID *)&pBuff3);
        if ((res == ERROR_SUCCESS) || (res == ERROR_MORE_DATA)) { // If the call succeeds,
            p3 = pBuff3;
            for (; dwRec > 0; dwRec--) {
                // Print the retrieved group information.
                printf("Name:      %ls\n"
                       "Comment:   %ls\n"
                       "flags:     %u\n"
                       "full_name: %ls\n"
                       "user_id:   %u\n"
                       "--------------------------------\n",
                       p3->usri1_name,
                       p3->usri1_comment,
                       p3->usri1_flags,
                       p3->usri1_full_name,
                       p3->usri1_user_id);

                // If there is more data, set the index.
                i = p3->usri1_next_index;
                p3++;
            }

            NetApiBufferFree(pBuff3); // Free the allocated memory.
        } else {
            printf("Error: %u\n", res);
        }

        // Continue while there is more data.
    } while (res == ERROR_MORE_DATA); // end do
}


int EnumLocalGroup()
/*
本程序的功能是显示本计算机上的所有的组，及其内的成员的一些信息。
本程序可以实现net localgroup的功能。

made at 2011.12.07
*/
{
    DWORD read{};
    DWORD total{};
    DWORD_PTR resume = 0;
    LPVOID buff{};
    DWORD ret = NetLocalGroupEnum(
        0, 1, reinterpret_cast<unsigned char **>(&buff), static_cast<DWORD>(-1), &read, &total, &resume);
    PLOCALGROUP_INFO_1 info = reinterpret_cast<PLOCALGROUP_INFO_1>(buff);

    for (DWORD i = 0; i < read; i++) {
        printf("GROUP: %S\n", info[i].lgrpi1_name);
        char comment[255];
        WideCharToMultiByte(CP_ACP, 0, info[i].lgrpi1_comment, -1, comment, 255, nullptr, nullptr);
        printf("COMMENT: %s\n", comment);

        DWORD entriesread{};
        ret = NetLocalGroupGetMembers(nullptr,
                                      info[i].lgrpi1_name,
                                      2,
                                      reinterpret_cast<unsigned char **>(&buff),
                                      1024,
                                      &entriesread,
                                      &total,
                                      &resume);
        PLOCALGROUP_MEMBERS_INFO_2 info2 = reinterpret_cast<PLOCALGROUP_MEMBERS_INFO_2>(buff);
        for (unsigned j = 0; j < entriesread; j++) {
            printf("\t域\\名:%S\n", info2[j].lgrmi2_domainandname);
            // printf("\tSID:%d\n", info[i].lgrmi2_sid);
            if (info2[j].lgrmi2_sidusage == SidTypeUser) {
                printf("\tSIDUSAGE:The account is a user account\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeGroup) {
                printf("\tSIDUSAGE:The account is a global group account\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeWellKnownGroup) {
                printf("\tSIDUSAGE:The account is a well-known group account (such as Everyone). \n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeDeletedAccount) {
                printf("\tSIDUSAGE:The account has been deleted\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeDomain) {
                printf("\tSIDUSAGE:SidTypeDomain\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeAlias) {
                printf("\tSIDUSAGE:SidTypeAlias\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeInvalid) {
                printf("\tSIDUSAGE:SidTypeInvalid\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeComputer) {
                printf("\tSIDUSAGE:SidTypeComputer\n");
            } else if (info2[j].lgrmi2_sidusage == SidTypeLabel) {
                printf("\tSIDUSAGE:SidTypeLabel\n");
            } else {
                printf("\tSIDUSAGE:未知\n");
            }
            printf("\n");
        }

        NetApiBufferFree(buff);
        printf("\n");
    }

    return 0;
}


int EnumShare()
/*
本功能可以列出本电脑上的所有的共享资源，相当于net share的功能，当然还有其他的一些信息没有列举出来。
made at 2011.12.08
*/
{
    PSHARE_INFO_502 p{}, p1{};
    DWORD er = 0, tr = 0, resume = 0;

    printf("共享名:            资源:                          注释               \n");
    printf("---------------------------------------------------------------------\n");

    (void)NetShareEnum(
        (LPWSTR)L".", 502, reinterpret_cast<LPBYTE *>(&p), static_cast<DWORD>(-1), &er, &tr, &resume);
    p1 = p;

    for (DWORD i = 1; i <= er; i++) {
        char comment[255];
        WideCharToMultiByte(0, 0, p->shi502_remark, -1, comment, 255, 0, 0);

        printf("%-20S%-30S%s\n", p->shi502_netname, p->shi502_path, comment);

        p++;
    }

    NetApiBufferFree(p1);
    printf("命令成功完成。\n\n");
    return 0;
}


int EnumShare(int argc, TCHAR * lpszArgv[])
/*
https://docs.microsoft.com/en-us/windows/win32/api/lmshare/nf-lmshare-netshareenum

调用示例：EnumShare(argc, argv);
输入参数是一个点即可，如：network.exe  .
*/
{
    PSHARE_INFO_502 BufPtr{}, p{};
    NET_API_STATUS res{};
    LPTSTR lpszServer = nullptr;
    DWORD er = 0, tr = 0, resume = 0, i{};

    switch (argc) {
    case 1:
        lpszServer = nullptr;
        break;
    case 2:
        lpszServer = lpszArgv[1];
        break;
    default:
        printf("Usage: NetShareEnum <servername>\n");
        return 0;
    }

    // Print a report header.
    printf("Share:              Local Path:                   Uses:   Descriptor:\n");
    printf("---------------------------------------------------------------------\n");

    // Call the NetShareEnum function; specify level 502.
    do // begin do
    {
        res = NetShareEnum(
            lpszServer, 502, reinterpret_cast<LPBYTE *>(&BufPtr), MAX_PREFERRED_LENGTH, &er, &tr, &resume);
        if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA) { // If the call succeeds,
            p = BufPtr;

            // Loop through the entries;
            //  print retrieved data.
            for (i = 1; i <= er; i++) {
                printf("%-20S%-30S%-8u", p->shi502_netname, p->shi502_path, p->shi502_current_uses);

                // Validate the value of the shi502_security_descriptor member.
                if (IsValidSecurityDescriptor(p->shi502_security_descriptor))
                    printf("Yes\n");
                else
                    printf("No\n");

                p++;
            }

            NetApiBufferFree(BufPtr); // Free the allocated buffer.
        } else {
            printf("Error: %lu\n", res);
        }
    }
    // Continue to call NetShareEnum while there are more entries.
    while (res == ERROR_MORE_DATA); // end do

    return 0;
}


void EnumWkstaUser()
/*
本功能可以列出已经登录到本电脑上的所有的用户。第一个好像不是，是啥具体不太清楚。
made at 2011.12.08
*/
{
    LPWKSTA_USER_INFO_0 pBuf{}, pTmpBuf{};
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;

    NetWkstaUserEnum((LPWSTR)L".",
                     0,
                     reinterpret_cast<LPBYTE *>(&pBuf),
                     static_cast<DWORD>(-1),
                     &dwEntriesRead,
                     &dwTotalEntries,
                     &dwResumeHandle);
    pTmpBuf = pBuf;

    for (DWORD i = 0; (i < dwEntriesRead); i++) {
        wprintf(L"%s\n", pTmpBuf->wkui0_username);
        pTmpBuf++;
    }

    NetApiBufferFree(pBuf);
}


int EnumWkstaUser(int argc, wchar_t * argv[])
// https://docs.microsoft.com/en-us/windows/win32/api/lmwksta/nf-lmwksta-netwkstauserenum
{
    LPWKSTA_USER_INFO_0 pBuf = nullptr;
    LPWKSTA_USER_INFO_0 pTmpBuf{};
    DWORD dwLevel = 0;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    DWORD i{};
    DWORD dwTotalCount = 0;
    NET_API_STATUS nStatus{};
    LPWSTR pszServerName = nullptr;

    if (argc > 2) {
        fwprintf(stderr, L"Usage: %s [\\\\ServerName]\n", argv[0]);
        return (1);
    }

    // The server is not the default local computer.
    if (argc == 2)
        pszServerName = argv[1];
    fwprintf(stderr, L"\nUsers currently logged on %s:\n", pszServerName);

    // Call the NetWkstaUserEnum function, specifying level 0.
    do // begin do
    {
        nStatus = NetWkstaUserEnum(pszServerName,
                                   dwLevel,
                                   reinterpret_cast<LPBYTE *>(&pBuf),
                                   dwPrefMaxLen,
                                   &dwEntriesRead,
                                   &dwTotalEntries,
                                   &dwResumeHandle);
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) { // If the call succeeds,
            if ((pTmpBuf = pBuf) != nullptr) {
                for (i = 0; (i < dwEntriesRead); i++) { // Loop through the entries.
                    assert(pTmpBuf != nullptr);
                    if (pTmpBuf == nullptr) {
                        // Only members of the Administrators local group
                        //  can successfully execute NetWkstaUserEnum
                        //  locally and on a remote server.
                        fprintf(stderr, "An access violation has occurred\n");
                        break;
                    }

                    wprintf(L"\t-- %s\n", pTmpBuf->wkui0_username); // Print the user logged on to the workstation.

                    pTmpBuf++;
                    dwTotalCount++;
                }
            }
        } else { // Otherwise, indicate a system error.
            fprintf(stderr, "A system error has occurred: %u\n", nStatus);
        }

        if (pBuf != nullptr) { // Free the allocated memory.
            NetApiBufferFree(pBuf);
            pBuf = nullptr;
        }
    }
    // Continue to call NetWkstaUserEnum while there are more entries.
    while (nStatus == ERROR_MORE_DATA); // end do

    // Check again for allocated memory.
    if (pBuf != nullptr)
        NetApiBufferFree(pBuf);

    // Print the final count of workstation users.
    fprintf(stderr, "\nTotal of %u entries enumerated\n", dwTotalCount);

    return 0;
}


int EnumSession(int argc, wchar_t * argv[])
// NetSessionEnum 枚举就不写了，相当于 net session。
// https://docs.microsoft.com/en-us/windows/win32/api/lmshare/nf-lmshare-netsessionenum
{
    LPSESSION_INFO_10 pBuf = nullptr;
    LPSESSION_INFO_10 pTmpBuf{};
    DWORD dwLevel = 10;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwResumeHandle = 0;
    DWORD i{};
    DWORD dwTotalCount = 0;
    LPTSTR pszServerName = nullptr;
    LPTSTR pszClientName = nullptr;
    LPTSTR pszUserName = nullptr;
    NET_API_STATUS nStatus{};

    // Check command line arguments.
    if (argc > 4) {
        wprintf(L"Usage: %s [\\\\ServerName] [\\\\ClientName] [UserName]\n", argv[0]);
        return (1);
    }

    if (argc >= 2)
        pszServerName = argv[1];

    if (argc >= 3)
        pszClientName = argv[2];

    if (argc == 4)
        pszUserName = argv[3];

    // Call the NetSessionEnum function, specifying level 10.
    do // begin do
    {
        nStatus = NetSessionEnum(pszServerName,
                                 pszClientName,
                                 pszUserName,
                                 dwLevel,
                                 reinterpret_cast<LPBYTE *>(&pBuf),
                                 dwPrefMaxLen,
                                 &dwEntriesRead,
                                 &dwTotalEntries,
                                 &dwResumeHandle);
        if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) { // If the call succeeds,
            if ((pTmpBuf = pBuf) != nullptr) {
                for (i = 0; (i < dwEntriesRead); i++) { // Loop through the entries.
                    assert(pTmpBuf != nullptr);
                    if (pTmpBuf == nullptr) {
                        fprintf(stderr, "An access violation has occurred\n");
                        break;
                    }

                    // Print the retrieved data.
                    wprintf(L"\n\tClient: %s\n", pTmpBuf->sesi10_cname);
                    wprintf(L"\tUser:   %s\n", pTmpBuf->sesi10_username);
                    printf("\tActive: %u\n", pTmpBuf->sesi10_time);
                    printf("\tIdle:   %u\n", pTmpBuf->sesi10_idle_time);

                    pTmpBuf++;
                    dwTotalCount++;
                }
            }
        } else { // Otherwise, indicate a system error.
            fprintf(stderr, "A system error has occurred: %u\n", nStatus);
        }

        if (pBuf != nullptr) { // Free the allocated memory.
            NetApiBufferFree(pBuf);
            pBuf = nullptr;
        }
    }
    // Continue to call NetSessionEnum while there are more entries.
    while (nStatus == ERROR_MORE_DATA); // end do

    // Check again for an allocated buffer.
    if (pBuf != nullptr)
        NetApiBufferFree(pBuf);

    // Print the final count of sessions enumerated.
    fprintf(stderr, "\nTotal of %u entries enumerated\n", dwTotalCount);

    return 0;
}


int EnumConnection(int argc, wchar_t * argv[])
// wprintf(L"Syntax: %s [ServerName] ShareName | \\\\ComputerName\n", argv[0]);
// https://docs.microsoft.com/en-us/windows/win32/api/lmshare/nf-lmshare-netconnectionenum
{
    DWORD res{}, i{}, er = 0, tr = 0, resume = 0;
    PCONNECTION_INFO_1 p{}, b{};
    LPTSTR lpszServer = nullptr, lpszShare = nullptr;

    if (argc > 2) {
        return 0;
    }

    switch (argc) {
    case 1:
        lpszServer = nullptr;
        break;
    case 2:
        lpszServer = argv[1]; // The server is not the default local computer.
        break;
    default:
        lpszServer = argv[1]; // The server is not the default local computer.
        break;
    }

    lpszShare = argv[argc - 1]; // ShareName is always the last argument.

    // Call the NetConnectionEnum function, specifying information level 1.
    res = NetConnectionEnum(
        lpszServer, lpszShare, 1, reinterpret_cast<LPBYTE *>(&p), MAX_PREFERRED_LENGTH, &er, &tr, &resume);
    if (res == 0) {   // If no error occurred,
        if (er > 0) { // If there were any results,
            b = p;

            // Loop through the entries; print user name and network name.
            for (i = 0; i < er; i++) {
                printf("%S\t%S\n", b->coni1_username, b->coni1_netname);
                b++;
            }

            NetApiBufferFree(p); // Free the allocated buffer.
        }
        // Otherwise, print a message depending on whether
        //  the qualifier parameter was a computer (\\ComputerName) or a share (ShareName).
        else {
            if (lpszShare[0] == '\\')
                printf("No connection to %S from %S\n",
                       (lpszServer == nullptr) ? TEXT("LocalMachine") : lpszServer,
                       lpszShare);
            else
                printf("No one connected to %S\\%S\n",
                       (lpszServer == nullptr) ? TEXT("\\\\LocalMachine") : lpszServer,
                       lpszShare);
        }
    } else { // Otherwise, print the error.
        printf("Error: %u\n", res);
    }

    return 0;
}


int EnumServer(int argc, wchar_t * argv[])
// NetServerEnum 就不写了，可以使用 WNetEnumResource。
// https://docs.microsoft.com/en-us/windows/win32/api/lmserver/nf-lmserver-netserverenum
{
    LPSERVER_INFO_101 pBuf = nullptr;
    LPSERVER_INFO_101 pTmpBuf{};
    DWORD dwLevel = 101;
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    DWORD dwTotalCount = 0;
    DWORD dwServerType = SV_TYPE_SERVER; // all servers
    DWORD dwResumeHandle = 0;
    NET_API_STATUS nStatus{};
    LPWSTR pszServerName = nullptr;
    LPWSTR pszDomainName = nullptr;
    DWORD i{};

    if (argc > 2) {
        fwprintf(stderr, L"Usage: %s [DomainName]\n", argv[0]);
        return (1);
    }

    // The request is not for the primary domain.
    if (argc == 2)
        pszDomainName = argv[1];

    // Call the NetServerEnum function to retrieve information
    //  for all servers, specifying information level 101.
    nStatus = NetServerEnum(pszServerName,
                            dwLevel,
                            reinterpret_cast<LPBYTE *>(&pBuf),
                            dwPrefMaxLen,
                            &dwEntriesRead,
                            &dwTotalEntries,
                            dwServerType,
                            pszDomainName,
                            &dwResumeHandle);
    if ((nStatus == NERR_Success) || (nStatus == ERROR_MORE_DATA)) { // If the call succeeds,
        if ((pTmpBuf = pBuf) != nullptr) {

            // Loop through the entries and print the data for all server types.
            for (i = 0; i < dwEntriesRead; i++) {
                assert(pTmpBuf != nullptr);
                if (pTmpBuf == nullptr) {
                    fprintf(stderr, "An access violation has occurred\n");
                    break;
                }

                printf("\tPlatform: %u\n", pTmpBuf->sv101_platform_id);
                wprintf(L"\tName:     %s\n", pTmpBuf->sv101_name);
                printf("\tVersion:  %u.%u\n", pTmpBuf->sv101_version_major, pTmpBuf->sv101_version_minor);
                printf("\tType:     %u", pTmpBuf->sv101_type);

                // Check to see if the server is a domain controller;
                //  if so, identify it as a PDC or a BDC.
                if (pTmpBuf->sv101_type & SV_TYPE_DOMAIN_CTRL)
                    wprintf(L" (PDC)");
                else if (pTmpBuf->sv101_type & SV_TYPE_DOMAIN_BAKCTRL)
                    wprintf(L" (BDC)");

                printf("\n");

                // Also print the comment associated with the server.
                wprintf(L"\tComment:  %s\n\n", pTmpBuf->sv101_comment);

                pTmpBuf++;
                dwTotalCount++;
            }

            // Display a warning if all available entries were
            //  not enumerated, print the number actually enumerated, and the total number available.
            if (nStatus == ERROR_MORE_DATA) {
                fprintf(stderr, "\nMore entries available!!!\n");
                fprintf(stderr, "Total entries: %u", dwTotalEntries);
            }

            printf("\nEntries enumerated: %u\n", dwTotalCount);
        } else {
            printf("No servers were found\n");
            printf("The buffer (bufptr) returned was NULL\n");
            printf("  entriesread: %u\n", dwEntriesRead);
            printf("  totalentries: %u\n", dwEntriesRead);
        }
    } else
        fprintf(stderr, "NetServerEnum failed with error: %u\n", nStatus);

    if (pBuf != nullptr) // Free the allocated buffer.
        NetApiBufferFree(pBuf);

    return 0;
}


int EnumServerDisk(int argc, wchar_t * argv[])
// https://docs.microsoft.com/en-us/windows/win32/api/lmserver/nf-lmserver-netserverdiskenum
{
    const int ENTRY_SIZE = 3; // Drive letter, colon, NULL
    LPTSTR pBuf = nullptr;
    DWORD dwLevel = 0; // level must be zero
    DWORD dwPrefMaxLen = MAX_PREFERRED_LENGTH;
    DWORD dwEntriesRead = 0;
    DWORD dwTotalEntries = 0;
    NET_API_STATUS nStatus{};
    LPWSTR pszServerName = nullptr;

    if (argc > 2) {
        fwprintf(stderr, L"Usage: %s [\\\\ServerName]\n", argv[0]);
        return (1);
    }

    // The server is not the default local computer.
    if (argc == 2)
        pszServerName = argv[1];

    // Call the NetServerDiskEnum function.
    nStatus = NetServerDiskEnum(pszServerName,
                                dwLevel,
                                reinterpret_cast<LPBYTE *>(&pBuf),
                                dwPrefMaxLen,
                                &dwEntriesRead,
                                &dwTotalEntries,
                                nullptr);
    if (nStatus == NERR_Success) { // If the call succeeds,
        LPTSTR pTmpBuf;

        if ((pTmpBuf = pBuf) != nullptr) {
            DWORD i{};
            DWORD dwTotalCount = 0;

            // Loop through the entries.
            for (i = 0; i < dwEntriesRead; i++) {
                assert(pTmpBuf != nullptr);
                if (pTmpBuf == nullptr) {
                    // On a remote computer, only members of the
                    //  Administrators or the Server Operators local group can execute NetServerDiskEnum.
                    fprintf(stderr, "An access violation has occurred\n");
                    break;
                }

                // Print drive letter, colon, nullptr for each drive;
                //   the number of entries actually enumerated; and the total number of entries available.
                fwprintf(stdout, L"\tDisk: %lS\n", pTmpBuf);

                pTmpBuf += ENTRY_SIZE;
                dwTotalCount++;
            }

            fprintf(stderr, "\nEntries enumerated: %u\n", dwTotalCount);
        }
    } else
        fprintf(stderr, "A system error has occurred: %u\n", nStatus);

    if (pBuf != nullptr) // Free the allocated buffer.
        NetApiBufferFree(pBuf);

    return 0;
}


void DisplayStruct(int i, LPNETRESOURCE lpnrLocal)
{
    printf("NETRESOURCE[%d] Scope: ", i);
    switch (lpnrLocal->dwScope) {
    case (RESOURCE_CONNECTED):
        printf("connected\n");
        break;
    case (RESOURCE_GLOBALNET):
        printf("all resources\n");
        break;
    case (RESOURCE_REMEMBERED):
        printf("remembered\n");
        break;
    default:
        printf("unknown scope %u\n", lpnrLocal->dwScope);
        break;
    }

    printf("NETRESOURCE[%d] Type: ", i);
    switch (lpnrLocal->dwType) {
    case (RESOURCETYPE_ANY):
        printf("any\n");
        break;
    case (RESOURCETYPE_DISK):
        printf("disk\n");
        break;
    case (RESOURCETYPE_PRINT):
        printf("print\n");
        break;
    default:
        printf("unknown type %u\n", lpnrLocal->dwType);
        break;
    }

    printf("NETRESOURCE[%d] DisplayType: ", i);
    switch (lpnrLocal->dwDisplayType) {
    case (RESOURCEDISPLAYTYPE_GENERIC):
        printf("generic\n");
        break;
    case (RESOURCEDISPLAYTYPE_DOMAIN):
        printf("domain\n");
        break;
    case (RESOURCEDISPLAYTYPE_SERVER):
        printf("server\n");
        break;
    case (RESOURCEDISPLAYTYPE_SHARE):
        printf("share\n");
        break;
    case (RESOURCEDISPLAYTYPE_FILE):
        printf("file\n");
        break;
    case (RESOURCEDISPLAYTYPE_GROUP):
        printf("group\n");
        break;
    case (RESOURCEDISPLAYTYPE_NETWORK):
        printf("network\n");
        break;
    default:
        printf("unknown display type %u\n", lpnrLocal->dwDisplayType);
        break;
    }

    printf("NETRESOURCE[%d] Usage: 0x%x = ", i, lpnrLocal->dwUsage);
    if (lpnrLocal->dwUsage & RESOURCEUSAGE_CONNECTABLE)
        printf("connectable ");
    if (lpnrLocal->dwUsage & RESOURCEUSAGE_CONTAINER)
        printf("container ");
    printf("\n");

    printf("NETRESOURCE[%d] Localname: %S\n", i, lpnrLocal->lpLocalName);
    printf("NETRESOURCE[%d] Remotename: %S\n", i, lpnrLocal->lpRemoteName);
    printf("NETRESOURCE[%d] Comment: %S\n", i, lpnrLocal->lpComment);
    printf("NETRESOURCE[%d] Provider: %S\n", i, lpnrLocal->lpProvider);
    printf("\n");
}


BOOL EnumResource(LPNETRESOURCE lpnr)
/*

调用代码示例：
    LPNETRESOURCE lpnr = nullptr;
    if (EnumResource(lpnr) == FALSE) {
        printf("Call to EnumerateFunc failed\n");
        return 1;
    } else
        return 0;

https://docs.microsoft.com/en-us/windows/win32/wnet/enumerating-network-resources
*/
{
    DWORD dwResult{}, dwResultEnum{};
    HANDLE hEnum{};
    DWORD cbBuffer = 16384;                  // 16K is a good size
    DWORD cEntries = static_cast<DWORD>(-1); // enumerate all possible entries
    LPNETRESOURCE lpnrLocal{};               // pointer to enumerated structures
    DWORD i{};

    // Call the WNetOpenEnum function to begin the enumeration.
    dwResult = WNetOpenEnum(RESOURCE_GLOBALNET, // all network resources
                            RESOURCETYPE_ANY,   // all resources
                            0,                  // enumerate all resources
                            lpnr,               // NULL first time the function is called
                            &hEnum);            // handle to the resource
    if (dwResult != NO_ERROR) {
        printf("WnetOpenEnum failed with error %u\n", dwResult);
        return FALSE;
    }

    // Call the GlobalAlloc function to allocate resources.
    lpnrLocal = static_cast<LPNETRESOURCE>(GlobalAlloc(GPTR, cbBuffer));
    if (lpnrLocal == nullptr) {
        printf("WnetOpenEnum failed with error %u\n", dwResult);
        //      NetErrorHandler(hwnd, dwResult, (LPSTR)"WNetOpenEnum");
        return FALSE;
    }

    do {
        ZeroMemory(lpnrLocal, cbBuffer); // Initialize the buffer.

        // Call the WNetEnumResource function to continue the enumeration.
        dwResultEnum = WNetEnumResource(hEnum,      // resource handle
                                        &cEntries,  // defined locally as -1
                                        lpnrLocal,  // LPNETRESOURCE
                                        &cbBuffer); // buffer size
        if (dwResultEnum == NO_ERROR) {             // If the call succeeds, loop through the structures.
            for (i = 0; i < cEntries; i++) {
                // Call an application-defined function to
                //  display the contents of the NETRESOURCE structures.
                DisplayStruct(i, &lpnrLocal[i]);

                // If the NETRESOURCE structure represents a container resource,
                //  call the EnumerateFunc function recursively.
                if (RESOURCEUSAGE_CONTAINER == (lpnrLocal[i].dwUsage & RESOURCEUSAGE_CONTAINER))
                    //          if(!EnumerateFunc(hwnd, hdc, &lpnrLocal[i]))
                    if (!EnumResource(&lpnrLocal[i]))
                        printf("EnumerateFunc returned FALSE\n");
                //            TextOut(hdc, 10, 10, "EnumerateFunc returned FALSE.", 29);
            }
        } else if (dwResultEnum != ERROR_NO_MORE_ITEMS) { // Process errors.
            printf("WNetEnumResource failed with error %u\n", dwResultEnum);
            //      NetErrorHandler(hwnd, dwResultEnum, (LPSTR)"WNetEnumResource");
            break;
        }
    } while (dwResultEnum != ERROR_NO_MORE_ITEMS); // End do.

    GlobalFree(static_cast<HGLOBAL>(lpnrLocal)); // Call the GlobalFree function to free the memory.

    // Call WNetCloseEnum to end the enumeration.
    dwResult = WNetCloseEnum(hEnum);
    if (dwResult != NO_ERROR) {
        // Process errors.
        printf("WNetCloseEnum failed with error %u\n", dwResult);
        //    NetErrorHandler(hwnd, dwResult, (LPSTR)"WNetCloseEnum");
        return FALSE;
    }

    return TRUE;
}


// NetScheduleJobEnum与此类似，更多的就不写了。
// NetGroupEnum
// NetAccessEnum
// NetWkstaTransportEnum
// NetFileEnum
// NetUserChangePassword 在另一个工程里，用于弱密码检测。


//////////////////////////////////////////////////////////////////////////////////////////////////


static int Usage(__in const wchar_t * name)
/*++
Routine Description:
    This routine prints the intended usage for this program.
Arguments:
    progName - NULL terminated string representing the name of the executable
--*/
{
    wprintf(L"%s user.\n", name);
    wprintf(L"%s localgroup.\n", name);
    wprintf(L"%s share.\n", name);
    wprintf(L"%s session.\n", name);
    wprintf(L"%s loggedUser.\n", name);
    wprintf(L"%s Connection.\n", name);
    wprintf(L"%s Server.\n", name);
    wprintf(L"%s ServerDisk.\n", name);
    wprintf(L"%s Resource.\n", name);

    return ERROR_SUCCESS;
}


int net(int argc, wchar_t * argv[])
{
    int ret = ERROR_SUCCESS;

    if (argc == 1) {
        return Usage(argv[0]);
    }

    if (argc == 2 && lstrcmpi(argv[1], TEXT("user")) == 0) {
        ret = UserEnum();
        NetQueryDisplayInfo(0, NULL);
        ret = UserEnum(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("localgroup")) == 0) {
        ret = EnumLocalGroup();
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("share")) == 0) {
        ret = EnumShare();
        ret = EnumShare(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("session")) == 0) {
        ret = EnumSession(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("loggedUser")) == 0) {
        EnumWkstaUser();
        ret = EnumWkstaUser(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("Connection")) == 0) {
        // ret = EnumConnection(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("Server")) == 0) {
        ret = EnumServer(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("ServerDisk")) == 0) {
        ret = EnumServerDisk(--argc, ++argv);
    } else if (argc == 2 && lstrcmpi(argv[1], TEXT("Resource")) == 0) {
        LPNETRESOURCE lpnr = nullptr;
        ret = EnumResource(lpnr);
    } else {
        ret = Usage(argv[0]);
    }

    return ret;
}
