import SwiftUI
import UniformTypeIdentifiers

struct CatalogSource: Identifiable {
    let id: String
    let name: String
    let directory: Bool
    let error: String
}

/// File-provider work is serialized off the main thread; UI publishes snapshots.
final class CatalogSourceModel: ObservableObject {
    static let shared = CatalogSourceModel()
    @Published var sources: [CatalogSource] = []
    @Published var busy = false
    @Published var error: String?
    @Published var generation = 0
    private let queue = DispatchQueue(label: "com.flynes.catalog.sources", qos: .userInitiated)
    private var initialized = false

    func initialize() {
        guard !initialized else { return }
        initialized = true
        perform { try CatalogSourceService.sharedInstance().prepareBuiltin() }
    }

    func perform(_ operation: @escaping () throws -> Void) {
        guard !busy else { return }
        busy = true
        error = nil
        queue.async {
            var failure: String?
            do { try operation() } catch { failure = error.localizedDescription }
            let rows = CatalogSourceService.sharedInstance().sources().compactMap { row -> CatalogSource? in
                guard let uuid = row["uuid"] as? String, let name = row["name"] as? String else { return nil }
                return CatalogSource(id: uuid, name: name,
                                     directory: (row["scope"] as? NSNumber)?.uint32Value == 2,
                                     error: row["error"] as? String ?? "")
            }
            DispatchQueue.main.async {
                self.sources = rows
                self.error = failure
                self.busy = false
                self.generation += 1
            }
        }
    }

    func prepareROM(_ canonicalID: String, completion: @escaping (Result<Data, Error>) -> Void) {
        queue.async {
            let result = Result { try CatalogSourceService.sharedInstance().romData(canonicalID: canonicalID) }
            DispatchQueue.main.async { completion(result) }
        }
    }
}

struct CatalogSourceManagementView: View {
    @ObservedObject private var model = CatalogSourceModel.shared
    @State private var showImporter = false
    @State private var pickDirectory = false
    @State private var reauthorizeSource: CatalogSource?

    var body: some View {
        List {
            if model.busy {
                HStack { ProgressView(); Text("library.source.working") }
            }
            if let error = model.error {
                Section { Text(error).foregroundStyle(.red) }
            }
            Section {
                Button { openPicker(directory: true) } label: {
                    Label("library.source.add_folder", systemImage: "folder.badge.plus")
                }
                Button { openPicker(directory: false) } label: {
                    Label("library.source.add_files", systemImage: "doc.badge.plus")
                }
                Text("library.source.formats").font(.footnote).foregroundStyle(.secondary)
            }
            Section("library.source.imported") {
                if model.sources.isEmpty { Text("library.source.none").foregroundStyle(.secondary) }
                ForEach(model.sources) { source in
                    VStack(alignment: .leading, spacing: 10) {
                        Label(source.name, systemImage: source.directory ? "folder" : "doc")
                            .font(.headline)
                        if !source.error.isEmpty {
                            Text(source.error).font(.footnote).foregroundStyle(.red)
                            Text("library.source.reauthorize_hint").font(.footnote).foregroundStyle(.secondary)
                        }
                        HStack {
                            Button("library.source.rescan") {
                                model.perform { try CatalogSourceService.sharedInstance().rescan(uuid: source.id) }
                            }
                            Button("library.source.reauthorize") {
                                reauthorizeSource = source
                                pickDirectory = source.directory
                                showImporter = true
                            }
                            Spacer()
                            Button(role: .destructive) {
                                model.perform { try CatalogSourceService.sharedInstance().remove(uuid: source.id) }
                            } label: { Label("library.source.remove", systemImage: "trash") }
                        }
                        .buttonStyle(.borderless)
                    }
                    .padding(.vertical, 6)
                }
            }
            Section {
                Text("library.source.remove_hint").font(.footnote).foregroundStyle(.secondary)
            }
        }
        .disabled(model.busy)
        .navigationTitle("library.sources")
        .onAppear { model.initialize() }
        .fileImporter(isPresented: $showImporter,
                      allowedContentTypes: pickDirectory ? [.folder] : [.data, .archive],
                      allowsMultipleSelection: !pickDirectory && reauthorizeSource == nil) { result in
            let replacing = reauthorizeSource
            let directory = pickDirectory
            reauthorizeSource = nil
            switch result {
            case .failure(let error): model.error = error.localizedDescription
            case .success(let urls):
                model.perform {
                    if let replacing = replacing, let url = urls.first {
                        try CatalogSourceService.sharedInstance().reauthorize(uuid: replacing.id, url: url)
                    } else {
                        // Each selected file is an independent source. Continue after a failed file.
                        var failures: [String] = []
                        for url in urls {
                            do { _ = try CatalogSourceService.sharedInstance().add(url: url, directory: directory) }
                            catch { failures.append(error.localizedDescription) }
                        }
                        if !failures.isEmpty {
                            throw NSError(domain: "com.flynes.sources", code: 1,
                                          userInfo: [NSLocalizedDescriptionKey: failures.joined(separator: "\n")])
                        }
                    }
                }
            }
        }
    }

    private func openPicker(directory: Bool) {
        reauthorizeSource = nil
        pickDirectory = directory
        showImporter = true
    }
}
