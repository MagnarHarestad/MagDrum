#include <JuceHeader.h>
class MainComponent final : public juce::AudioAppComponent, private juce::Button::Listener, private juce::ListBoxModel {
public:
 MainComponent():rows("Samples",this){
  setSize(900,620); title.setText("MagDrum",juce::dontSendNotification); title.setFont(juce::Font(30.0f,juce::Font::bold)); title.setColour(juce::Label::textColourId,juce::Colour(0xfff4f0e8));
  subtitle.setText("Your drum libraries, in one place.",juce::dontSendNotification); subtitle.setColour(juce::Label::textColourId,juce::Colour(0xffaaa49b)); addAndMakeVisible(title);addAndMakeVisible(subtitle);
  scan.setButtonText("Add sample folder");scan.addListener(this);addAndMakeVisible(scan); play.setButtonText("Play selected");play.addListener(this);addAndMakeVisible(play);
  filter.setTextToShowWhenEmpty("Filter by filename or folder",juce::Colour(0xff77736c));filter.onTextChange=[this]{refresh();};addAndMakeVisible(filter);rows.setRowHeight(36);addAndMakeVisible(rows);
  formats.registerBasicFormats();transport.addChangeListener(this);setAudioChannels(0,2);initDb();refresh();
 }
 ~MainComponent()override{shutdownAudio();transport.removeChangeListener(this);transport.setSource(nullptr);source.reset();}
 void prepareToPlay(int n,double r)override{transport.prepareToPlay(n,r);} void getNextAudioBlock(const juce::AudioSourceChannelInfo&i)override{transport.getNextAudioBlock(i);} void releaseResources()override{transport.releaseResources();}
 void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff101215));g.setColour(juce::Colour(0xff25282c));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(20.0f),12.0f,1.0f);g.setColour(juce::Colour(0xffff8a42));g.fillRoundedRectangle(22,22,5,44,2);}
 void resized()override{auto a=getLocalBounds().reduced(38,28);auto h=a.removeFromTop(84);title.setBounds(h.removeFromTop(42));subtitle.setBounds(h);a.removeFromTop(16);auto c=a.removeFromTop(42);scan.setBounds(c.removeFromLeft(180));c.removeFromLeft(10);play.setBounds(c.removeFromLeft(140));c.removeFromLeft(18);filter.setBounds(c);a.removeFromTop(14);rows.setBounds(a);}
 int getNumRows()override{return visible.size();}
 void paintListBoxItem(int n,juce::Graphics&g,int w,int h,bool selected)override{if(!juce::isPositiveAndBelow(n,visible.size()))return;g.fillAll(selected?juce::Colour(0xff30343a):juce::Colour(0xff17191c));auto f=visible.getReference(n);g.setColour(juce::Colour(0xfff0eee9));g.setFont(15);g.drawText(f.getFileName(),12,3,w-24,20,juce::Justification::centredLeft,true);g.setColour(juce::Colour(0xff8e918f));g.setFont(12);g.drawText(f.getParentDirectory().getFullPathName(),12,21,w-24,h-21,juce::Justification::centredLeft,true);}
private:
 void initDb(){auto f=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MagDrum").getChildFile("samples.sqlite3");f.getParentDirectory().createDirectory();if(db.open(f))db.executeSQL("CREATE TABLE IF NOT EXISTS samples(path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0)");}
 void buttonClicked(juce::Button*b)override{if(b==&scan){chooser=std::make_unique<juce::FileChooser>("Choose a folder of WAV samples",juce::File(),juce::String());chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[this](const juce::FileChooser&c){auto d=c.getResult();if(d.isDirectory())scanFolder(d);});}else if(b==&play)playSelected();}
 void scanFolder(const juce::File&d){juce::DirectoryIterator it(d,true,"*.wav",juce::File::findFiles);while(it.next()){auto f=it.getFile();std::unique_ptr<juce::AudioFormatReader>r(formats.createReaderFor(f));if(!r)continue;auto p=f.getFullPathName().replace("'","''"),n=f.getFileName().replace("'","''");db.executeSQL("INSERT OR IGNORE INTO samples(path,name,duration,channels,sample_rate) VALUES('"+p+"','"+n+"',"+juce::String(r->lengthInSamples/r->sampleRate)+","+juce::String(r->numChannels)+","+juce::String(r->sampleRate)+")");}refresh();}
 void refresh(){all.clear();visible.clear();if(db.openedOk()){juce::SQLiteStatement q(db,"SELECT path FROM samples ORDER BY name COLLATE NOCASE");while(q.next())all.add(juce::File(q.getColumnValue(0).toString()));}auto t=filter.getText().trim().toLowerCase();for(auto&f:all)if(t.isEmpty()||f.getFileName().toLowerCase().contains(t)||f.getParentDirectory().getFullPathName().toLowerCase().contains(t))visible.add(f);rows.updateContent();rows.repaint();}
 void playSelected(){auto n=rows.getSelectedRow();if(!juce::isPositiveAndBelow(n,visible.size()))return;auto*r=formats.createReaderFor(visible.getReference(n));if(!r)return;transport.stop();transport.setSource(nullptr);source.reset(new juce::AudioFormatReaderSource(r,true));transport.setSource(source.get(),0,nullptr,r->sampleRate);transport.start();}
 void changeListenerCallback(juce::ChangeBroadcaster*)override{}
 juce::Label title,subtitle;juce::TextButton scan,play;juce::TextEditor filter;juce::ListBox rows;juce::AudioFormatManager formats;juce::AudioTransportSource transport;juce::SQLiteDatabase db;juce::Array<juce::File>all,visible;std::unique_ptr<juce::AudioFormatReaderSource>source;std::unique_ptr<juce::FileChooser>chooser;
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
class MagDrumApplication final:public juce::JUCEApplication{
public:const juce::String getApplicationName()override{return"MagDrum";}const juce::String getApplicationVersion()override{return"0.1.0";}bool moreThanOneInstanceAllowed()override{return true;}void initialise(const juce::String&)override{window=std::make_unique<Window>();}void shutdown()override{window.reset();}void systemRequestedQuit()override{quit();}void anotherInstanceStarted(const juce::String&)override{}
private:class Window final:public juce::DocumentWindow{public:Window():DocumentWindow("MagDrum",juce::Colour(0xff101215),allButtons){setUsingNativeTitleBar(true);setContentOwned(new MainComponent(),true);centreWithSize(940,660);setVisible(true);}void closeButtonPressed()override{juce::JUCEApplication::getInstance()->systemRequestedQuit();}};std::unique_ptr<Window>window;
};
START_JUCE_APPLICATION(MagDrumApplication)
