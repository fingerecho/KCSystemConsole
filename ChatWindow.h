#pragma once

#include <QWidget>

#include <LLMQore/Conversation.hpp>

class QLineEdit;
class QPushButton;
class QTextEdit;

namespace LLMQore {
class OpenAIClient;
}

class ChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWindow(QWidget *parent = nullptr);

private:
    void sendMessage();
    void appendChunk(const QString &chunk);
    void finishRequest();
    void insertText(const QString &text);

    LLMQore::OpenAIClient *m_client = nullptr;
    LLMQore::Conversation m_conversation;

    QTextEdit *m_messages = nullptr;
    QLineEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;

    bool m_busy = false;
    bool m_assistantStarted = false;
};
