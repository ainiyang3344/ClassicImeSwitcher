#include "StartupTask.h"
#include "WindowsInput.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QXmlStreamWriter>
#include <taskschd.h>
#include <wrl/client.h>
#include <comdef.h>

namespace {
using Microsoft::WRL::ComPtr;
QString taskName() { return QStringLiteral("LYLab.ClassicImeSwitcher.") + WindowsInput::userSid(); }
HRESULT openFolder(ComPtr<ITaskFolder> &folder)
{
    ComPtr<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service));
    if (SUCCEEDED(hr)) hr = service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (SUCCEEDED(hr)) hr = service->GetFolder(_bstr_t(L"\\"), &folder);
    return hr;
}
_bstr_t bstr(const QString &value) { return _bstr_t(reinterpret_cast<LPCWSTR>(value.utf16())); }
}

QString StartupTask::definition(const QString &executable, const QString &sid)
{
    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("Task"));
    writer.writeDefaultNamespace(QStringLiteral("http://schemas.microsoft.com/windows/2004/02/mit/task"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1.2"));
    writer.writeStartElement(QStringLiteral("Triggers"));
    writer.writeStartElement(QStringLiteral("LogonTrigger"));
    writer.writeTextElement(QStringLiteral("UserId"), sid);
    writer.writeEndElement(); writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("Principals"));
    writer.writeStartElement(QStringLiteral("Principal"));
    writer.writeAttribute(QStringLiteral("id"), QStringLiteral("CurrentUser"));
    writer.writeTextElement(QStringLiteral("UserId"), sid);
    writer.writeTextElement(QStringLiteral("LogonType"), QStringLiteral("InteractiveToken"));
    writer.writeTextElement(QStringLiteral("RunLevel"), QStringLiteral("HighestAvailable"));
    writer.writeEndElement(); writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("Settings"));
    writer.writeTextElement(QStringLiteral("MultipleInstancesPolicy"), QStringLiteral("IgnoreNew"));
    writer.writeTextElement(QStringLiteral("DisallowStartIfOnBatteries"), QStringLiteral("false"));
    writer.writeTextElement(QStringLiteral("StopIfGoingOnBatteries"), QStringLiteral("false"));
    writer.writeTextElement(QStringLiteral("ExecutionTimeLimit"), QStringLiteral("PT0S"));
    writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("Actions"));
    writer.writeAttribute(QStringLiteral("Context"), QStringLiteral("CurrentUser"));
    writer.writeStartElement(QStringLiteral("Exec"));
    writer.writeTextElement(QStringLiteral("Command"), QDir::toNativeSeparators(executable));
    writer.writeTextElement(QStringLiteral("Arguments"), QStringLiteral("--background"));
    writer.writeTextElement(QStringLiteral("WorkingDirectory"), QDir::toNativeSeparators(QFileInfo(executable).absolutePath()));
    writer.writeEndElement(); writer.writeEndElement(); writer.writeEndElement();
    writer.writeEndDocument();
    return xml;
}

bool StartupTask::enabled()
{
    ComPtr<ITaskFolder> folder;
    ComPtr<IRegisteredTask> task;
    if (FAILED(openFolder(folder)) || FAILED(folder->GetTask(bstr(taskName()), &task))) return false;
    VARIANT_BOOL active = VARIANT_FALSE;
    ComPtr<ITaskDefinition> definition;
    ComPtr<IPrincipal> principal;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    TASK_RUNLEVEL_TYPE runLevel = TASK_RUNLEVEL_LUA;
    _bstr_t path, arguments;
    LONG count = 0;
    return SUCCEEDED(task->get_Enabled(&active)) && active == VARIANT_TRUE
        && SUCCEEDED(task->get_Definition(&definition))
        && SUCCEEDED(definition->get_Principal(&principal))
        && SUCCEEDED(principal->get_RunLevel(&runLevel)) && runLevel == TASK_RUNLEVEL_HIGHEST
        && SUCCEEDED(definition->get_Actions(&actions))
        && SUCCEEDED(actions->get_Count(&count)) && count == 1
        && SUCCEEDED(actions->get_Item(1, &action)) && SUCCEEDED(action.As(&exec))
        && SUCCEEDED(exec->get_Path(path.GetAddress())) && SUCCEEDED(exec->get_Arguments(arguments.GetAddress()))
        && QDir::fromNativeSeparators(QString::fromWCharArray(path)).compare(QCoreApplication::applicationFilePath(), Qt::CaseInsensitive) == 0
        && QString::fromWCharArray(arguments) == QStringLiteral("--background");
}

bool StartupTask::setEnabled(bool enable, QString *error)
{
    ComPtr<ITaskFolder> folder;
    HRESULT hr = openFolder(folder);
    if (SUCCEEDED(hr)) {
        if (enable) {
            const QString sid = WindowsInput::userSid();
            ComPtr<IRegisteredTask> task;
            hr = folder->RegisterTask(bstr(taskName()), bstr(definition(QCoreApplication::applicationFilePath(), sid)), TASK_CREATE_OR_UPDATE,
                _variant_t(bstr(sid)), _variant_t(), TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(), &task);
        } else {
            hr = folder->DeleteTask(bstr(taskName()), 0);
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr = S_OK;
        }
    }
    if (FAILED(hr)) {
        *error = QStringLiteral("开机启动计划任务设置失败（0x%1）。").arg(static_cast<quint32>(hr), 8, 16, u'0');
        return false;
    }
    return true;
}
