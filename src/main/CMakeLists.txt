add_executable(tmms
main.cpp
)

add_executable(ai_server
ai_main/ai_server.cpp
../ai/AIModule.cpp
../ai/server/AIServer.cpp
../ai/http/HttpRequest.cpp
../ai/http/HttpResponse.cpp
../ai/http/HttpContext.cpp
../ai/http/HttpClient.cpp
../ai/llm/DoubaoLLM.cpp
../ai/embedding/DoubaoEmbedding.cpp
../ai/vector/SQLiteVectorStore.cpp
../ai/rag/TextSplitter.cpp
../ai/rag/PromptTemplate.cpp
../ai/rag/RAGService.cpp
../ai/storage/MySQLClient.cpp
../ai/auth/PasswordHasher.cpp
../ai/auth/JWT.cpp
../ai/auth/AuthService.cpp
../ai/storage/UserRepo.cpp
../ai/storage/ConversationRepo.cpp
../ai/storage/MessageRepo.cpp
)

target_link_libraries(tmms
base
network
${JSONCPP_LIBRARIES}
pthread
)

target_link_libraries(ai_server
base
network
${JSONCPP_LIBRARIES}
${CURL_LIBRARIES}
sqlite3
pthread
mysqlclient
ssl
crypto
)

target_compile_definitions(tmms PRIVATE PROJECT_ROOT="${PROJECT_SOURCE_DIR}")
target_compile_definitions(ai_server PRIVATE PROJECT_ROOT="${PROJECT_SOURCE_DIR}")

install(TARGETS tmms RUNTIME DESTINATION sbin)
install(TARGETS ai_server RUNTIME DESTINATION sbin)