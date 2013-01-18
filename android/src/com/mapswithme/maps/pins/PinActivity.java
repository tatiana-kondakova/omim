package com.mapswithme.maps.pins;

import java.util.List;

import com.mapswithme.maps.R;
import com.mapswithme.maps.pins.pins.Icon;
import com.mapswithme.maps.pins.pins.Pin;
import com.mapswithme.maps.pins.pins.PinSet;

import android.os.Bundle;
import android.content.Intent;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Menu;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.AdapterView;
import android.widget.EditText;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.Spinner;
import android.widget.TextView;

public class PinActivity extends AbstractPinActivity
{
  public static final String PIN = "pin";
  public static final String PIN_ICON_ID = "pin";
  public static final String PIN_SET = "pin_set";
  public static final int REQUEST_CODE_SET = 567890;
  private Spinner mSpinner;
  private Pin mPin;
  private EditText mName;
  private TextView mSetName;

  @Override
  public void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.pin);
    mPin = mManager.getPin(getIntent().getIntExtra(PIN, -1));
    if (mPin == null)
    {
      mPin = mManager.createNewPin();
    }

    setUpViews();
  }

  private void setUpViews()
  {
    (mSpinner = (Spinner) findViewById(R.id.pin_color)).setAdapter(new SpinnerAdapter(this, mManager.getIcons()));
    mSpinner.setOnItemSelectedListener(new OnItemSelectedListener()
    {

      @Override
      public void onItemSelected(AdapterView<?> arg0, View arg1, int arg2, long arg3)
      {
        mPin.setIcon(((SpinnerAdapter) arg0.getAdapter()).getItem(arg2));
      }

      @Override
      public void onNothingSelected(AdapterView<?> arg0)
      {

      }
    });
    int i = 0;
    int id = mPin.getIcon().getDrawableId();
    List<Icon> icons = mManager.getIcons();
    for (i = 0; i < icons.size() && icons.get(i).getDrawableId() != id; i++)
    {

    }
    mSpinner.setSelection(i);
    findViewById(R.id.pin_sets).setOnClickListener(new OnClickListener()
    {

      @Override
      public void onClick(View v)
      {
        startActivityForResult(
            new Intent(PinActivity.this, ChoosePinSetActivity.class).putExtra(PIN_SET,
                mManager.getSetId(mPin.getPinSet())), REQUEST_CODE_SET);
      }
    });
    mSetName = (TextView) findViewById(R.id.pin_button_set_name);
    mSetName.setText(mPin.getPinSet() != null ? mPin.getPinSet().getName() : getString(android.R.string.unknownName));
    mName = (EditText) findViewById(R.id.pin_name);
    mName.setText(mPin.getName());
    mName.addTextChangedListener(new TextWatcher()
    {

      @Override
      public void onTextChanged(CharSequence s, int start, int before, int count)
      {
        mPin.setName(s.toString());
      }

      @Override
      public void beforeTextChanged(CharSequence s, int start, int count, int after)
      {
      }

      @Override
      public void afterTextChanged(Editable s)
      {
      }
    });
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent data)
  {
    if (requestCode == REQUEST_CODE_SET && resultCode == RESULT_OK)
    {
      PinSet set = mManager.getSet(data.getIntExtra(PIN_SET, -1));
      if (set != null)
      {
        mPin.setPinSet(set);
        mSetName.setText(set.getName());
      }
    }
    super.onActivityResult(requestCode, resultCode, data);
  }

  @Override
  public boolean onCreateOptionsMenu(Menu menu)
  {
    getMenuInflater().inflate(R.menu.activity_pin, menu);
    return true;
  }
}
