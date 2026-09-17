#pragma once

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QtGlobal>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/Tools>

class ExecuteCommandTool : public LLMQore::BaseTool
{
    Q_OBJECT
public:
    explicit ExecuteCommandTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "execute_command"; }
    QString displayName() const override { return "Execute Command"; }
    QString description() const override
    {
        return "在项目目录执行终端命令，用于完成实际任务（查看文件/目录内容、创建或删除"
               "文件与目录、查看系统信息等）。参数：'command'（必填，命令名，例如 ls、"
               "dir、pwd、cat、touch、mkdir、echo），'args'（可选，命令参数）。创建文件"
               "用 touch 文件名；创建目录用 mkdir 目录名；写入内容用 echo 内容 > 文件名。"
               "仅允许白名单中的安全命令。";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties",
             QJsonObject{
                 {"command",
                  QJsonObject{
                      {"type", "string"},
                      {"description", "The command to execute, e.g. ls, dir, pwd, cat, "
                                     "touch, mkdir, echo"}}},
                 {"args",
                  QJsonObject{
                      {"type", "string"},
                      {"description", "Optional arguments for the command"}}}}},
            {"required", QJsonArray{"command"}}};
    }

    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMQore::ToolResult {
            const QString command = input.value("command").toString().trimmed();
            if (command.isEmpty())
                return LLMQore::ToolResult::error(QStringLiteral("'command' is required"));

            const QString name = command.section(QLatin1Char(' '), 0, 0).toLower();
            if (!allowedCommands().contains(name))
                return LLMQore::ToolResult::error(
                    QStringLiteral("command '%1' is not allowed").arg(name));

            const QString args = input.value("args").toString().trimmed();

            if (name == "touch")
                return runTouch(command, args);
            if (name == "mkdir" || name == "md")
                return runMkdir(command, args);

            return runInShell(command, args);
        });
    }

private:
    static const QSet<QString> &allowedCommands()
    {
        static const QSet<QString> cmds = {
            // listing / info
            QStringLiteral("ls"),     QStringLiteral("dir"),
            QStringLiteral("pwd"),    QStringLiteral("tree"),
            QStringLiteral("stat"),   QStringLiteral("du"),
            QStringLiteral("df"),     QStringLiteral("whoami"),
            QStringLiteral("hostname"), QStringLiteral("uname"),
            QStringLiteral("printenv"), QStringLiteral("env"),
            QStringLiteral("where"),  QStringLiteral("which"),
            QStringLiteral("date"),   QStringLiteral("time"),
            // reading / text processing
            QStringLiteral("cat"),    QStringLiteral("type"),
            QStringLiteral("head"),   QStringLiteral("tail"),
            QStringLiteral("wc"),     QStringLiteral("grep"),
            QStringLiteral("find"),   QStringLiteral("sort"),
            QStringLiteral("uniq"),   QStringLiteral("cut"),
            QStringLiteral("awk"),    QStringLiteral("sed"),
            QStringLiteral("basename"), QStringLiteral("dirname"),
            // creating / writing
            QStringLiteral("touch"),  QStringLiteral("mkdir"),
            QStringLiteral("md"),     QStringLiteral("rmdir"),
            QStringLiteral("rd"),     QStringLiteral("echo"),
            QStringLiteral("printf"), QStringLiteral("tee"),
            // move / copy / remove
            QStringLiteral("cp"),     QStringLiteral("copy"),
            QStringLiteral("mv"),     QStringLiteral("move"),
            QStringLiteral("ren"),    QStringLiteral("rename"),
            QStringLiteral("rm"),     QStringLiteral("del"),
            QStringLiteral("erase"),
        };
        return cmds;
    }

    static QStringList extractArguments(const QString &command, const QString &args)
    {
        QStringList list = QProcess::splitCommand(command);
        if (list.size() > 1)
            list.removeFirst();
        else
            list.clear();
        list << QProcess::splitCommand(args);
        return list;
    }

    static LLMQore::ToolResult runTouch(const QString &command, const QString &args)
    {
        const QStringList targets = extractArguments(command, args);
        if (targets.isEmpty())
            return LLMQore::ToolResult::error(QStringLiteral("touch: no file specified"));

        QStringList created;
        for (const QString &target : targets) {
            QFile file(target);
            if (file.exists()) {
                created << target;
                continue;
            }
            if (!file.open(QIODevice::WriteOnly))
                return LLMQore::ToolResult::error(
                    QStringLiteral("touch: cannot create '%1': %2").arg(target, file.errorString()));
            file.close();
            created << target;
        }
        return LLMQore::ToolResult::text(
            QStringLiteral("created: %1").arg(created.join(QStringLiteral(", "))));
    }

    static LLMQore::ToolResult runMkdir(const QString &command, const QString &args)
    {
        const QStringList dirs = extractArguments(command, args);
        if (dirs.isEmpty())
            return LLMQore::ToolResult::error(QStringLiteral("mkdir: no directory specified"));

        QStringList created;
        for (const QString &dir : dirs) {
            if (dir == QLatin1String("-p") || dir == QLatin1String("--parents"))
                continue;
            if (!QDir().mkpath(dir))
                return LLMQore::ToolResult::error(
                    QStringLiteral("mkdir: cannot create '%1'").arg(dir));
            created << dir;
        }
        return LLMQore::ToolResult::text(
            QStringLiteral("created directory(s): %1").arg(created.join(QStringLiteral(", "))));
    }

    static LLMQore::ToolResult runInShell(const QString &command, const QString &args)
    {
        QString full = command;
        if (!args.isEmpty())
            full += QLatin1Char(' ') + args;

        QString program;
        QStringList shellArgs;
#if defined(Q_OS_WIN)
        program = QStringLiteral("cmd.exe");
        shellArgs = QStringList{"/c", full};
#else
        program = QStringLiteral("/bin/sh");
        shellArgs = QStringList{"-c", full};
#endif

        QProcess process;
        process.start(program, shellArgs);
        if (!process.waitForStarted(5000))
            return LLMQore::ToolResult::error(QStringLiteral("failed to start shell"));
        if (!process.waitForFinished(30000)) {
            process.kill();
            process.waitForFinished(2000);
            return LLMQore::ToolResult::error(QStringLiteral("command timed out"));
        }

        QByteArray out = process.readAllStandardOutput();
        QByteArray err = process.readAllStandardError();
        const int exitCode = process.exitCode();

        constexpr int kMaxOutput = 64 * 1024;
        if (out.size() > kMaxOutput)
            out = out.left(kMaxOutput) + "\n... (truncated)";
        if (err.size() > kMaxOutput)
            err = err.left(kMaxOutput) + "\n... (truncated)";

        QStringList parts;
        if (!out.trimmed().isEmpty())
            parts << QString::fromLocal8Bit(out).trimmed();
        if (!err.trimmed().isEmpty())
            parts << QStringLiteral("stderr:\n%1").arg(QString::fromLocal8Bit(err).trimmed());
        if (parts.isEmpty())
            parts << QStringLiteral("(no output)");

        QString result = parts.join(QLatin1Char('\n'));
        if (exitCode != 0)
            return LLMQore::ToolResult::error(
                QStringLiteral("(exit code %1)\n%2").arg(exitCode).arg(result));
        return LLMQore::ToolResult::text(result);
    }
};
