#include "ChatWindow.h"
#include "ExecuteCommandTool.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <LLMQore/Clients>

namespace {

QString summarize(const QString &text, int maxLength)
{
    if (text.length() <= maxLength)
        return text;
    return text.left(maxLength) + QStringLiteral("\n... (truncated)");
}

} // namespace

ChatWindow::ChatWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("my-chat"));

    const QString baseUrl
        = qEnvironmentVariable("MY_CHAT_BASE_URL", QStringLiteral("https://api.deepseek.com/v1"));
    const QString apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY");
    const QString model = qEnvironmentVariable("MY_CHAT_MODEL", QStringLiteral("deepseek-chat"));

    m_client = new LLMQore::OpenAIClient(baseUrl, apiKey, model, this);
    m_client->tools()->addTool(new ExecuteCommandTool(m_client));

    auto *layout = new QVBoxLayout(this);

    m_messages = new QTextEdit(this);
    m_messages->setReadOnly(true);
    m_messages->setAcceptRichText(false);

    auto *inputRow = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Type a message and press Enter"));
    m_sendButton = new QPushButton(tr("Send"), this);
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton);

    layout->addWidget(m_messages, 1);
    layout->addLayout(inputRow);

    if (apiKey.isEmpty())
        insertText(tr("Set MY_CHAT_API_KEY (and optionally MY_CHAT_BASE_URL / "
                      "MY_CHAT_MODEL) before sending.\n"));

    connect(m_sendButton, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_input, &QLineEdit::returnPressed, this, &ChatWindow::sendMessage);

    connect(m_client, &LLMQore::BaseClient::chunkReceived, this,
            [this](const LLMQore::RequestID &, const QString &chunk) { appendChunk(chunk); });

    connect(m_client, &LLMQore::BaseClient::toolStarted, this,
            [this](const LLMQore::RequestID &, const QString &, const QString &,
                   const QJsonObject &) {
                insertText(tr("\n正在执行命令...\n"));
            });

    connect(m_client, &LLMQore::BaseClient::toolResultReady, this,
            [this](const LLMQore::RequestID &, const QString &, const QString &,
                   const QString &result) {
                insertText(tr("命令执行结果：\n%1\n").arg(summarize(result, 400)));
            });

    connect(m_client, &LLMQore::BaseClient::requestFinalized, this,
            [this](const LLMQore::RequestID &, const LLMQore::CompletionInfo &info) {
                m_conversation = info.conversation;
            });

    connect(m_client, &LLMQore::BaseClient::requestCompleted, this,
            [this](const LLMQore::RequestID &, const QString &) { finishRequest(); });

    connect(m_client, &LLMQore::BaseClient::requestFailed, this,
            [this](const LLMQore::RequestID &, const QString &error) {
                insertText(tr("\nError: %1\n").arg(error));
                finishRequest();
            });
}

void ChatWindow::sendMessage()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty() || m_busy)
        return;

    m_input->clear();
    insertText(tr("\nYou: %1\n").arg(text));
    m_assistantStarted = false;
    m_busy = true;
    m_sendButton->setEnabled(false);

    m_conversation.addUser(text);
    m_client->ask(m_conversation);
}

void ChatWindow::appendChunk(const QString &chunk)
{
    if (!m_assistantStarted) {
        insertText(tr("\nAssistant: "));
        m_assistantStarted = true;
    }
    insertText(chunk);
}

void ChatWindow::finishRequest()
{
    m_busy = false;
    m_assistantStarted = false;
    m_sendButton->setEnabled(true);
}

void ChatWindow::insertText(const QString &text)
{
    m_messages->moveCursor(QTextCursor::End);
    m_messages->insertPlainText(text);
    m_messages->moveCursor(QTextCursor::End);
    m_messages->ensureCursorVisible();
}
