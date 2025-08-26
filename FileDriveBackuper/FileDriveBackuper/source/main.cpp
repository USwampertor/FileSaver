#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <curl/curl.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"
#include "ImGuiFileDialog.h"

#include "FileSaver.h"

int main(int argc, char* argv[]) {
  // Setup SDL
  FileSaver fileSaver;
  bool* openDemo = new bool(true);

  // Setup SDL
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("Error: SDL_Init(): %s\n", SDL_GetError());
    return -1;
  }

  // GL 3.0 + GLSL 130
  const char* glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  // Create window
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  // Create window
  SDL_Window* window = SDL_CreateWindow("File Drive Saver",
                                        640, 480,
                                        SDL_WINDOW_RESIZABLE | 
                                        SDL_WINDOW_OPENGL | 
                                        SDL_WINDOW_TRANSPARENT);
  if (!window) {
    printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    SDL_Quit();
    return -1;
  }

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  if (!gl_context)
  {
    printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }

  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Our state
  ImVec4 clear_color = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  bool show_file_dialog = false;
  std::string file_path_name;
  std::string file_path;

  // Demo state
  std::string selected_file;
  std::string selected_path;
  std::string filter = ".*,.psd,.pbd,.jpg,.png,.bmp,.tiff,.tga,.pdf,.doc,.docx,.xls,.xlsx,.zip,.rar";

  // Main loop
  bool done = false;
  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) done = true;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == SDL_GetWindowID(window)) done = true;
    }

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Get the main viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Set the next window position and size to cover the entire viewport
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Begin the main window with no decoration
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_MenuBar |
      ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));


    if (ImGui::Begin("MainWindow", nullptr, window_flags)) {
      // Menu Bar
      // File dialog buttons
      
      // Main content area - fills the remaining space
      ImVec2 content_size = ImGui::GetContentRegionAvail();

      // Display some content in the main area
      ImGui::BeginChild("ContentArea", content_size, true);
      {
        ImGui::Text("File Backup Saver");
        ImGui::Separator();
        if (ImGui::Button("Open File")) {
          IGFD::FileDialogConfig config;
          config.path = ".";
          ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", filter.c_str(), config);
        }
        ImGui::SameLine();
        if (!file_path_name.empty()) {
          ImGui::Text("Selected File: %s", file_path_name.c_str());
          ImGui::Text("File Path: %s", file_path.c_str());
        }
        else {
          ImGui::Text("No file selected. Use Open File first to choose a file.");
        }
        ImGui::Text("Right-Click over the Directory (C:/My/Directory/ to write the full path you want)");

        ImGui::Spacing();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
          1000.0f / io.Framerate, io.Framerate);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Debug: File Path Set: %s", fileSaver.m_isFilePathSet ? "Yes" : "No");
        ImGui::PushItemWidth(ImGui::GetWindowWidth() / 2);
        ImGui::SliderFloat("Seconds between saves: ", 
                           &fileSaver.m_saveInterval, 
                           10.0f, 
                           600.0f, 
                           "%1.0f");
        ImGui::PopItemWidth();

        std::string buttonLabel = (!fileSaver.m_isSaving ? "Start" : "Stop");
        buttonLabel += " Saving";
        ImGui::Separator();

        ImGui::Text("Backblaze B2 Cloud Storage Configuration");
        ImGui::PushItemWidth(ImGui::GetWindowWidth() / 2);
        ImGui::InputText("Backblaze Key ID", &fileSaver.m_b2Credentials.accountId);
        ImGui::InputText("Backblaze Application Key", &fileSaver.m_b2Credentials.applicationKey);
        ImGui::InputText("Bucket Name", &fileSaver.m_b2Credentials.bucketName);
        ImGui::PopItemWidth();

        ImGui::Separator();
        bool wasAuthenticated = fileSaver.m_b2Credentials.isAuthenticated;
        if (wasAuthenticated) {
          ImGui::BeginDisabled();
        }

        if (ImGui::Button("Authenticate with Backblaze B2")) {
          if (fileSaver.m_b2Credentials.authenticate()) {
            fileSaver.m_logger += "Backblaze B2 authentication successful!\n";
          }
          else {
            fileSaver.m_logger += "Backblaze B2 authentication failed!\n";
          }
        }

        if (wasAuthenticated) {
          ImGui::EndDisabled();
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), " Authenticated");
        }

        ImGui::Spacing();

        if (!fileSaver.m_isFilePathSet || !fileSaver.m_b2Credentials.isAuthenticated) {
          ImGui::BeginDisabled();
        }

        ImGui::Text("You can use the button below to start/stop saving your file to the cloud.");
        ImGui::Text("File will be backed up every %.1f seconds", fileSaver.m_saveInterval);

        if (ImGui::Button(buttonLabel.c_str(), ImVec2(120, 40))) {
          fileSaver.setSaveFileThread(!fileSaver.m_isSaving);
          if (fileSaver.m_isSaving) {
            fileSaver.m_logger += "Backup process started\n";
          }
          else {
            fileSaver.m_logger += "Backup process stopped\n";
          }
        }

        if (!fileSaver.m_isFilePathSet || !fileSaver.m_b2Credentials.isAuthenticated) {
          ImGui::EndDisabled();
          ImGui::SameLine();
          if (!fileSaver.m_isFilePathSet) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select a file first!");
          }
          else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Authenticate first!");
          }
        }

        ImGui::Separator();
        ImGui::Text("Logger:");
        ImGui::BeginChild("ScrollingText", ImVec2(0, 0), true,
          ImGuiWindowFlags_HorizontalScrollbar |
          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::TextWrapped("%s", fileSaver.m_logger.c_str());

        // Auto-scroll to bottom if new text was added
        static bool auto_scroll = true;
        if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
          ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

      }
      ImGui::EndChild();
    }

    

    ImGui::End();
    ImGui::PopStyleVar(3);

    // File Dialog
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
      if (ImGuiFileDialog::Instance()->IsOk()) {
        file_path_name = ImGuiFileDialog::Instance()->GetFilePathName();
        file_path = ImGuiFileDialog::Instance()->GetCurrentPath();

        // Set the file path in your FileSaver
        fileSaver.m_filePath = file_path_name;
        fileSaver.m_isFilePathSet = true;
        fileSaver.m_logger += "File selected: " + file_path_name + "\n";
        fileSaver.m_logger += "File size: " +
          std::to_string(std::filesystem::file_size(file_path_name)) +
          " bytes\n";
      }

      else {
        fileSaver.m_logger += "File selection canceled.\n";
      }
      // Always close the dialog
      ImGuiFileDialog::Instance()->Close();
    }

    

    // Rendering
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w,
      clear_color.y * clear_color.w,
      clear_color.z * clear_color.w,
      clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }

    SDL_GL_SwapWindow(window);
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}